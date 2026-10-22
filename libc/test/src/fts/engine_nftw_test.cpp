//===-- Tests for private nftw traversal preparation ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/libc_errno.h"
#include "src/fcntl/fcntl.h"
#include "src/fcntl/open.h"
#include "src/fts/engine.h"
#include "src/fts/linux_io.h"
#include "src/stdio/snprintf.h"
#include "src/stdlib/mkdtemp.h"
#include "src/string/strcmp.h"
#include "src/string/strcpy.h"
#include "src/string/strlen.h"
#include "src/sys/stat/chmod.h"
#include "src/sys/stat/mkdir.h"
#include "src/unistd/close.h"
#include "src/unistd/getcwd.h"
#include "src/unistd/rmdir.h"
#include "src/unistd/symlink.h"
#include "src/unistd/unlink.h"
#include "test/UnitTest/Test.h"
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/stat.h>

namespace {
struct Allocation {
  void *pointer;
  size_t size;
};
Allocation allocations[4096];
size_t live = 0;
bool tracking = false;
bool failed = false;
int fail_after = -1;
size_t moves = 0;
// Hermetic malloc is monotonic. Reuse private storage across independent
// failure-injection rounds, while the allocation ledger still detects leaks.
alignas(max_align_t) unsigned char arena[512 * 1024];
size_t used = 0;

void *test_allocate(size_t size) {
  constexpr size_t alignment = alignof(max_align_t);
  size_t padding = (alignment - used % alignment) % alignment;
  if (padding > sizeof(arena) - used ||
      size > sizeof(arena) - used - padding)
    __builtin_trap();
  void *result = arena + used + padding;
  used += padding + size;
  return result;
}

size_t find_allocation(void *pointer) {
  for (size_t i = 0; i < live; ++i)
    if (allocations[i].pointer == pointer)
      return i;
  return live;
}

void remember(void *pointer, size_t size) {
  if (!pointer)
    return;
  if (live == sizeof(allocations) / sizeof(allocations[0]))
    __builtin_trap();
  allocations[live++] = {pointer, size};
}

bool fail_allocation() {
  if (!tracking || fail_after < 0)
    return false;
  if (fail_after-- != 0)
    return false;
  failed = true;
  return true;
}

void begin_tracking(int failure = -1) {
  if (live != 0)
    __builtin_trap();
  failed = false;
  moves = 0;
  used = 0;
  fail_after = failure;
  tracking = true;
}
} // namespace

extern "C" void *__real_malloc(size_t);
extern "C" void *__real_realloc(void *, size_t);
extern "C" void __real_free(void *);

extern "C" void *__wrap_malloc(size_t size) {
  if (fail_allocation())
    return nullptr;
  void *pointer = tracking ? test_allocate(size) : __real_malloc(size);
  if (tracking)
    remember(pointer, size);
  return pointer;
}

extern "C" void *__wrap_realloc(void *old, size_t size) {
  if (fail_allocation())
    return nullptr;
  size_t index = find_allocation(old);
  void *pointer;
  if (tracking && old && index != live) {
    // Allocate before freeing to force a different address, not merely growth.
    pointer = test_allocate(size);
    if (pointer) {
      size_t count = allocations[index].size;
      if (count > size)
        count = size;
      for (size_t i = 0; i < count; ++i)
        static_cast<char *>(pointer)[i] = static_cast<char *>(old)[i];
      ++moves;
    }
  } else {
    pointer = tracking && !old ? test_allocate(size) : __real_realloc(old, size);
  }
  if (tracking && pointer) {
    if (index != live)
      allocations[index] = allocations[--live];
    remember(pointer, size);
  }
  return pointer;
}

extern "C" void __wrap_free(void *pointer) {
  if (tracking) {
    size_t index = find_allocation(pointer);
    if (index != live) {
      allocations[index] = allocations[--live];
      return;
    }
  }
  __real_free(pointer);
}

namespace {
using LIBC_NAMESPACE::fts::PrepareResult;
using LIBC_NAMESPACE::fts::WalkPolicy;
using LIBC_NAMESPACE::fts::walk_close;
using LIBC_NAMESPACE::fts::walk_open;
using LIBC_NAMESPACE::fts::walk_prepare_directory;
using LIBC_NAMESPACE::fts::walk_read;
using LIBC_NAMESPACE::fts::walk_set;

constexpr int PHYSICAL = FTS_PHYSICAL | FTS_NOCHDIR;
constexpr int LOGICAL = FTS_LOGICAL | FTS_NOCHDIR;

struct WalkOwner {
  FTS *stream;
  ~WalkOwner() {
    if (stream)
      walk_close(stream);
  }
};

struct RestoreMode {
  const char *path;
  ~RestoreMode() { LIBC_NAMESPACE::chmod(path, 0700); }
};

class LlvmLibcFtsNftwTest : public LIBC_NAMESPACE::testing::Test {
protected:
  char root[64];

  void path(char *out, const char *suffix) {
    LIBC_NAMESPACE::snprintf(out, 4096, "%s/%s", root, suffix);
  }
  void directory(const char *suffix) {
    char name[4096];
    path(name, suffix);
    ASSERT_EQ(LIBC_NAMESPACE::mkdir(name, 0700), 0);
  }
  void file(const char *suffix) {
    char name[4096];
    path(name, suffix);
    int fd = LIBC_NAMESPACE::open(name, O_CREAT | O_WRONLY, 0600);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(LIBC_NAMESPACE::close(fd), 0);
  }
  void link(const char *target, const char *suffix) {
    char name[4096];
    path(name, suffix);
    ASSERT_EQ(LIBC_NAMESPACE::symlink(target, name), 0);
  }
  FTS *open(char *name, int options = PHYSICAL,
            WalkPolicy policy = WalkPolicy::Nftw) {
    char *paths[] = {name, nullptr};
    return walk_open(paths, options, nullptr, policy);
  }
  FTS *open(int options = PHYSICAL,
            WalkPolicy policy = WalkPolicy::Nftw) {
    return open(root, options, policy);
  }
  static int descriptor_count() {
    int count = 0;
    for (int fd = 0; fd < 128; ++fd)
      count += LIBC_NAMESPACE::fcntl(fd, F_GETFD) >= 0;
    return count;
  }
  void deep_tree() {
    char suffix[2048] = {};
    for (int i = 0; i < 24; ++i) {
      size_t len = LIBC_NAMESPACE::strlen(suffix);
      if (len)
        suffix[len++] = '/';
      for (int j = 0; j < 48; ++j)
        suffix[len++] = 'x';
      suffix[len] = '\0';
      directory(suffix);
    }
    file("first");
    file("second");
  }
  void SetUp() override {
    LIBC_NAMESPACE::strcpy(root, "fts-nftw-test-XXXXXX");
    ASSERT_NE(LIBC_NAMESPACE::mkdtemp(root), static_cast<char *>(nullptr));
  }
  void TearDown() override {
    // No cleanup walk uses the strict policy: it must remove loop symlinks too.
    WalkOwner owner{open(PHYSICAL, WalkPolicy::Fts)};
    ASSERT_NE(owner.stream, static_cast<FTS *>(nullptr));
    while (FTSENT *entry = walk_read(owner.stream)) {
      if (entry->fts_info == FTS_DP)
        ASSERT_EQ(LIBC_NAMESPACE::rmdir(entry->fts_accpath), 0);
      else if (entry->fts_info != FTS_D)
        ASSERT_EQ(LIBC_NAMESPACE::unlink(entry->fts_accpath), 0);
    }
  }
};
} // namespace

TEST_F(LlvmLibcFtsNftwTest, EmptyPreparationConsumesItsPostorderEvent) {
  char name[4096];
  path(name, ""); // Preserve the root's trailing slash, even for an empty list.
  WalkOwner owner{open(name)};
  ASSERT_NE(owner.stream, static_cast<FTS *>(nullptr));
  FTSENT *entry = walk_read(owner.stream, WalkPolicy::Nftw);
  ASSERT_NE(entry, static_cast<FTSENT *>(nullptr));
  ASSERT_EQ(int(entry->fts_info), FTS_D);
  EXPECT_EQ(int(walk_prepare_directory(owner.stream, WalkPolicy::Nftw)),
            int(PrepareResult::Empty));
  EXPECT_EQ(int(libc_errno), 0);
  EXPECT_EQ(owner.stream->fts_cur, entry);
  EXPECT_EQ(int(entry->fts_info), FTS_DP);
  EXPECT_STREQ(entry->fts_path, name);
  EXPECT_EQ(entry->fts_pathlen, LIBC_NAMESPACE::strlen(entry->fts_path));
  EXPECT_EQ(int(walk_prepare_directory(owner.stream, WalkPolicy::Nftw)),
            int(PrepareResult::Error));
  EXPECT_EQ(int(libc_errno), EINVAL);
  EXPECT_EQ(int(entry->fts_info), FTS_DP);
  EXPECT_EQ(walk_read(owner.stream, WalkPolicy::Nftw),
            static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(int(libc_errno), 0);
}

TEST_F(LlvmLibcFtsNftwTest, PreparedChildrenAreTransferredExactlyOnce) {
  file("file");
  int descriptors = descriptor_count();
  char cwd[4096], after[4096];
  ASSERT_NE(LIBC_NAMESPACE::getcwd(cwd, sizeof(cwd)),
            static_cast<char *>(nullptr));
  WalkOwner owner{open()};
  ASSERT_NE(owner.stream, static_cast<FTS *>(nullptr));
  FTSENT *parent = walk_read(owner.stream, WalkPolicy::Nftw);
  ASSERT_NE(parent, static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(int(walk_prepare_directory(owner.stream, WalkPolicy::Nftw)),
            int(PrepareResult::Ready));
  EXPECT_EQ(int(libc_errno), 0);
  EXPECT_STREQ(parent->fts_path, root);
  FTSENT *child = owner.stream->fts_child;
  ASSERT_NE(child, static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(int(walk_prepare_directory(owner.stream, WalkPolicy::Nftw)),
            int(PrepareResult::Error));
  EXPECT_EQ(int(libc_errno), EINVAL);
  EXPECT_EQ(owner.stream->fts_child, child);
  EXPECT_EQ(walk_read(owner.stream, WalkPolicy::Nftw), child);
  EXPECT_EQ(owner.stream->fts_child, static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(int(child->fts_info), FTS_F);
  EXPECT_EQ(walk_read(owner.stream, WalkPolicy::Nftw), parent);
  EXPECT_EQ(int(parent->fts_info), FTS_DP);
  EXPECT_EQ(walk_read(owner.stream, WalkPolicy::Nftw),
            static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(int(libc_errno), 0);
  EXPECT_EQ(descriptor_count(), descriptors);
  ASSERT_NE(LIBC_NAMESPACE::getcwd(after, sizeof(after)),
            static_cast<char *>(nullptr));
  EXPECT_STREQ(cwd, after);
}

TEST_F(LlvmLibcFtsNftwTest, NestedEmptyDirectoryDoesNotRepeat) {
  directory("empty");
  directory("full");
  file("full/file");
  WalkOwner owner{open()};
  ASSERT_NE(owner.stream, static_cast<FTS *>(nullptr));
  int directories = 0, posts = 0, empty = 0, files = 0;
  while (FTSENT *entry = walk_read(owner.stream, WalkPolicy::Nftw)) {
    if (entry->fts_info == FTS_D) {
      ++directories;
      PrepareResult result =
          walk_prepare_directory(owner.stream, WalkPolicy::Nftw);
      ASSERT_TRUE(result == PrepareResult::Ready ||
                  result == PrepareResult::Empty);
      if (result == PrepareResult::Empty) {
        EXPECT_STREQ(entry->fts_name, "empty");
        ++empty;
      }
    } else if (entry->fts_info == FTS_DP) {
      EXPECT_NE(LIBC_NAMESPACE::strcmp(entry->fts_name, "empty"), 0);
      ++posts;
    } else {
      EXPECT_EQ(int(entry->fts_info), FTS_F);
      ++files;
    }
  }
  EXPECT_EQ(int(libc_errno), 0);
  EXPECT_EQ(directories, 3);
  EXPECT_EQ(posts, 2);
  EXPECT_EQ(empty, 1);
  EXPECT_EQ(files, 1);
}

TEST_F(LlvmLibcFtsNftwTest, InvalidPreparationDoesNotConsumeInstructions) {
  file("file");
  WalkOwner owner{open()};
  ASSERT_NE(owner.stream, static_cast<FTS *>(nullptr));
  EXPECT_EQ(int(walk_prepare_directory(owner.stream, WalkPolicy::Nftw)),
            int(PrepareResult::Error));
  EXPECT_EQ(int(libc_errno), EINVAL);
  FTSENT *entry = walk_read(owner.stream, WalkPolicy::Nftw);
  ASSERT_NE(entry, static_cast<FTSENT *>(nullptr));
  ASSERT_EQ(walk_set(owner.stream, entry, FTS_SKIP), 0);
  EXPECT_EQ(int(walk_prepare_directory(owner.stream, WalkPolicy::Nftw)),
            int(PrepareResult::Error));
  EXPECT_EQ(int(libc_errno), EINVAL);
  EXPECT_EQ(int(entry->fts_instr), FTS_SKIP);
  EXPECT_EQ(int(entry->fts_info), FTS_D);
  EXPECT_EQ(walk_read(owner.stream, WalkPolicy::Nftw), entry);
  EXPECT_EQ(int(entry->fts_info), FTS_DP);
}

TEST_F(LlvmLibcFtsNftwTest, StrictPolicyRejectsIncompatibleOptionsOnlyPrivately) {
  const int cases[] = {FTS_PHYSICAL, PHYSICAL | FTS_NOSTAT, PHYSICAL | FTS_SEEDOT};
  for (int options : cases) {
    EXPECT_EQ(open(options), static_cast<FTS *>(nullptr));
    EXPECT_EQ(int(libc_errno), EINVAL);
    WalkOwner ordinary{open(options, WalkPolicy::Fts)};
    ASSERT_NE(ordinary.stream, static_cast<FTS *>(nullptr));
    ASSERT_NE(walk_read(ordinary.stream), static_cast<FTSENT *>(nullptr));
    if (!(options & FTS_SEEDOT)) {
      EXPECT_EQ(int(walk_prepare_directory(ordinary.stream, WalkPolicy::Fts)),
                int(PrepareResult::Error));
      EXPECT_EQ(int(libc_errno), EINVAL);
      EXPECT_EQ(int(ordinary.stream->fts_cur->fts_info), FTS_D);
    }
  }
  WalkOwner logical{open(FTS_LOGICAL)};
  ASSERT_NE(logical.stream, static_cast<FTS *>(nullptr));
  EXPECT_NE(logical.stream->fts_options & FTS_NOCHDIR, 0);
}

TEST_F(LlvmLibcFtsNftwTest, MissingPreparedDirectoryIsFatalNotUnreadable) {
  directory("gone");
  char name[4096];
  path(name, "gone");
  WalkOwner owner{open(name)};
  ASSERT_NE(owner.stream, static_cast<FTS *>(nullptr));
  FTSENT *entry = walk_read(owner.stream, WalkPolicy::Nftw);
  ASSERT_NE(entry, static_cast<FTSENT *>(nullptr));
  ASSERT_EQ(LIBC_NAMESPACE::rmdir(name), 0);
  EXPECT_EQ(int(walk_prepare_directory(owner.stream, WalkPolicy::Nftw)),
            int(PrepareResult::Error));
  EXPECT_EQ(int(libc_errno), ENOENT);
  EXPECT_EQ(int(entry->fts_info), FTS_ERR);
  EXPECT_EQ(entry->fts_errno, ENOENT);
  EXPECT_STREQ(entry->fts_path, name);
  EXPECT_NE(owner.stream->fts_options & FTS_STOP, 0);
  EXPECT_EQ(owner.stream->fts_child, static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(walk_read(owner.stream, WalkPolicy::Nftw),
            static_cast<FTSENT *>(nullptr));
}

TEST_F(LlvmLibcFtsNftwTest, OrdinaryReadRetainsNonAccessDnr) {
  directory("gone");
  char name[4096];
  path(name, "gone");
  WalkOwner owner{open(name, PHYSICAL, WalkPolicy::Fts)};
  ASSERT_NE(owner.stream, static_cast<FTS *>(nullptr));
  FTSENT *entry = walk_read(owner.stream);
  ASSERT_NE(entry, static_cast<FTSENT *>(nullptr));
  ASSERT_EQ(LIBC_NAMESPACE::rmdir(name), 0);
  EXPECT_EQ(walk_read(owner.stream), entry);
  EXPECT_EQ(int(entry->fts_info), FTS_DNR);
  EXPECT_EQ(entry->fts_errno, ENOENT);
  EXPECT_EQ(owner.stream->fts_options & FTS_STOP, 0);
}

TEST_F(LlvmLibcFtsNftwTest, OnlyMissingTargetsBecomeSlnInStrictPolicy) {
  file("file");
  link("loop", "loop");
  link("file/child", "notdir");
  link("missing", "dangling");
  struct {
    const char *name;
    int error;
  } cases[] = {{"loop", ELOOP}, {"notdir", ENOTDIR}};
  for (auto test : cases) {
    char name[4096];
    path(name, test.name);
    EXPECT_EQ(open(name, LOGICAL), static_cast<FTS *>(nullptr));
    EXPECT_EQ(int(libc_errno), test.error);
    WalkOwner ordinary{open(name, LOGICAL, WalkPolicy::Fts)};
    ASSERT_NE(ordinary.stream, static_cast<FTS *>(nullptr));
    FTSENT *entry = walk_read(ordinary.stream);
    ASSERT_NE(entry, static_cast<FTSENT *>(nullptr));
    EXPECT_EQ(int(entry->fts_info), FTS_SLNONE);
    EXPECT_TRUE(S_ISLNK(entry->fts_statp->st_mode));
  }
  char name[4096];
  path(name, "dangling");
  WalkOwner owner{open(name, LOGICAL)};
  ASSERT_NE(owner.stream, static_cast<FTS *>(nullptr));
  FTSENT *entry = walk_read(owner.stream, WalkPolicy::Nftw);
  ASSERT_NE(entry, static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(int(entry->fts_info), FTS_SLNONE);
  EXPECT_EQ(entry->fts_errno, 0);
  EXPECT_TRUE(S_ISLNK(entry->fts_statp->st_mode));
  EXPECT_EQ(entry->fts_statp->st_size, decltype(entry->fts_statp->st_size)(7));
}

TEST_F(LlvmLibcFtsNftwTest, FatalChildStatDiscardsPreparationAndClosesStream) {
  file("first");
  file("second");
  link("loop", "loop");
  int descriptors = descriptor_count();
  begin_tracking();
  FTS *stream = open(LOGICAL);
  FTSENT *entry = stream ? walk_read(stream, WalkPolicy::Nftw) : nullptr;
  PrepareResult result = entry ? walk_prepare_directory(stream, WalkPolicy::Nftw)
                               : PrepareResult::Ready;
  int error = libc_errno;
  bool stopped = stream && (stream->fts_options & FTS_STOP);
  bool retired = stream && !stream->fts_child;
  bool restored = entry && !LIBC_NAMESPACE::strcmp(entry->fts_path, root);
  if (stream)
    walk_close(stream);
  tracking = false;
  EXPECT_EQ(int(result), int(PrepareResult::Error));
  EXPECT_EQ(error, ELOOP);
  EXPECT_TRUE(stopped);
  EXPECT_TRUE(retired);
  EXPECT_TRUE(restored);
  EXPECT_EQ(live, size_t(0));
  EXPECT_EQ(descriptor_count(), descriptors);
}

TEST_F(LlvmLibcFtsNftwTest, ReadWithoutPreparationAlsoStopsAtFatalStat) {
  link("loop", "loop");
  WalkOwner owner{open(LOGICAL)};
  ASSERT_NE(owner.stream, static_cast<FTS *>(nullptr));
  ASSERT_NE(walk_read(owner.stream, WalkPolicy::Nftw),
            static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(walk_read(owner.stream, WalkPolicy::Nftw),
            static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(int(libc_errno), ELOOP);
  EXPECT_NE(owner.stream->fts_options & FTS_STOP, 0);
  EXPECT_EQ(owner.stream->fts_child, static_cast<FTSENT *>(nullptr));
}

TEST_F(LlvmLibcFtsNftwTest, FatalLaterRootReclaimsEarlierRoots) {
  char missing[4096];
  path(missing, "missing");
  char *paths[] = {root, missing, nullptr};
  begin_tracking();
  FTS *stream = walk_open(paths, PHYSICAL, nullptr, WalkPolicy::Nftw);
  int error = libc_errno;
  bool rejected = stream == nullptr;
  if (stream)
    walk_close(stream);
  tracking = false;
  EXPECT_TRUE(rejected);
  EXPECT_EQ(error, ENOENT);
  EXPECT_EQ(live, size_t(0));
}

TEST_F(LlvmLibcFtsNftwTest, AgainAndFollowStopAtTheOriginalStatError) {
  file("file");
  link("loop", "loop");
  char name[4096];
  path(name, "file");
  WalkOwner file_owner{open(name)};
  ASSERT_NE(file_owner.stream, static_cast<FTS *>(nullptr));
  FTSENT *entry = walk_read(file_owner.stream, WalkPolicy::Nftw);
  ASSERT_NE(entry, static_cast<FTSENT *>(nullptr));
  ASSERT_EQ(LIBC_NAMESPACE::unlink(name), 0);
  ASSERT_EQ(walk_set(file_owner.stream, entry, FTS_AGAIN), 0);
  EXPECT_EQ(walk_read(file_owner.stream, WalkPolicy::Nftw),
            static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(int(libc_errno), ENOENT);
  EXPECT_EQ(entry->fts_errno, ENOENT);
  EXPECT_NE(file_owner.stream->fts_options & FTS_STOP, 0);

  path(name, "loop");
  WalkOwner link_owner{open(name)};
  ASSERT_NE(link_owner.stream, static_cast<FTS *>(nullptr));
  entry = walk_read(link_owner.stream, WalkPolicy::Nftw);
  ASSERT_NE(entry, static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(int(entry->fts_info), FTS_SL);
  ASSERT_EQ(walk_set(link_owner.stream, entry, FTS_FOLLOW), 0);
  EXPECT_EQ(walk_read(link_owner.stream, WalkPolicy::Nftw),
            static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(int(libc_errno), ELOOP);
  EXPECT_EQ(entry->fts_errno, ELOOP);
  EXPECT_NE(link_owner.stream->fts_options & FTS_STOP, 0);
}

TEST_F(LlvmLibcFtsNftwTest, PendingChildFollowErrorKeepsTheTreeCloseable) {
  file("file");
  link("loop", "loop");
  begin_tracking();
  FTS *stream = open();
  bool followed = false;
  if (stream && walk_read(stream, WalkPolicy::Nftw) &&
      walk_prepare_directory(stream, WalkPolicy::Nftw) == PrepareResult::Ready) {
    for (FTSENT *child = stream->fts_child; child; child = child->fts_link)
      if (child->fts_info == FTS_SL) {
        walk_set(stream, child, FTS_FOLLOW);
        followed = true;
      }
    while (walk_read(stream, WalkPolicy::Nftw)) {
    }
  }
  int error = libc_errno;
  bool stopped = stream && (stream->fts_options & FTS_STOP);
  if (stream)
    walk_close(stream);
  tracking = false;
  EXPECT_TRUE(followed);
  EXPECT_TRUE(stopped);
  EXPECT_EQ(error, ELOOP);
  EXPECT_EQ(live, size_t(0));
}

TEST_F(LlvmLibcFtsNftwTest, PreparedLogicalWalkKeepsAliasesAndAncestorCycles) {
  directory("dir");
  file("dir/file");
  link("..", "dir/cycle");
  link("dir", "alias");
  WalkOwner owner{open(LOGICAL)};
  ASSERT_NE(owner.stream, static_cast<FTS *>(nullptr));
  int cycles = 0, files = 0, directories = 0;
  while (FTSENT *entry = walk_read(owner.stream, WalkPolicy::Nftw)) {
    if (entry->fts_info == FTS_D) {
      ++directories;
      EXPECT_EQ(int(walk_prepare_directory(owner.stream, WalkPolicy::Nftw)),
                int(PrepareResult::Ready));
    } else if (entry->fts_info == FTS_DC) {
      ++cycles;
      ASSERT_NE(entry->fts_cycle, static_cast<FTSENT *>(nullptr));
      EXPECT_EQ(entry->fts_cycle->fts_level, 0);
    } else if (entry->fts_info == FTS_F) {
      ++files;
    } else {
      EXPECT_EQ(int(entry->fts_info), FTS_DP);
    }
  }
  EXPECT_EQ(int(libc_errno), 0);
  EXPECT_EQ(directories, 3);
  EXPECT_EQ(cycles, 2);
  EXPECT_EQ(files, 2);
}

TEST_F(LlvmLibcFtsNftwTest, PreparedEarlyCloseRetiresPendingChildren) {
  directory("dir");
  file("dir/file");
  file("other");
  int descriptors = descriptor_count();
  begin_tracking();
  FTS *stream = open();
  FTSENT *entry = stream ? walk_read(stream, WalkPolicy::Nftw) : nullptr;
  PrepareResult result = entry ? walk_prepare_directory(stream, WalkPolicy::Nftw)
                               : PrepareResult::Error;
  if (stream)
    walk_close(stream);
  tracking = false;
  EXPECT_EQ(int(result), int(PrepareResult::Ready));
  EXPECT_EQ(live, size_t(0));
  EXPECT_EQ(descriptor_count(), descriptors);
}

TEST_F(LlvmLibcFtsNftwTest, PreparationRebasesPathsAfterForcedReallocation) {
  deep_tree();
  begin_tracking();
  FTS *stream = open();
  bool valid = stream != nullptr;
  int depth = 0;
  if (stream) {
    while (FTSENT *entry = walk_read(stream, WalkPolicy::Nftw)) {
      if (entry->fts_level > depth)
        depth = entry->fts_level;
      char before[4096];
      LIBC_NAMESPACE::strcpy(before, entry->fts_path);
      if (entry->fts_info == FTS_D) {
        PrepareResult result = walk_prepare_directory(stream, WalkPolicy::Nftw);
        valid &= result == PrepareResult::Ready || result == PrepareResult::Empty;
        valid &= !LIBC_NAMESPACE::strcmp(before, entry->fts_path);
        if (result == PrepareResult::Error)
          break;
        for (FTSENT *child = stream->fts_child; child; child = child->fts_link) {
          valid &= child->fts_path == stream->fts_path;
          valid &= child->fts_accpath == stream->fts_path;
        }
      }
      valid &= entry->fts_pathlen == LIBC_NAMESPACE::strlen(entry->fts_path);
      for (FTSENT *p = entry; p->fts_level >= FTS_ROOTLEVEL; p = p->fts_parent)
        valid &= p->fts_path == stream->fts_path;
    }
    valid &= libc_errno == 0;
    walk_close(stream);
  }
  tracking = false;
  EXPECT_TRUE(valid);
  EXPECT_EQ(depth, 24);
  EXPECT_GT(moves, size_t(0));
  EXPECT_EQ(live, size_t(0));
}

TEST_F(LlvmLibcFtsNftwTest, EveryAllocationFailureLeavesPreparedWalkCloseable) {
  deep_tree();
  int descriptors = descriptor_count();
  bool completed = false;
  for (int failure = 0; failure < 512 && !completed; ++failure) {
    begin_tracking(failure);
    FTS *stream = open();
    bool valid_error = true;
    if (stream) {
      while (FTSENT *entry = walk_read(stream, WalkPolicy::Nftw)) {
        if (entry->fts_info != FTS_D)
          continue;
        char before[4096];
        LIBC_NAMESPACE::strcpy(before, entry->fts_path);
        PrepareResult result = walk_prepare_directory(stream, WalkPolicy::Nftw);
        if (result == PrepareResult::Error) {
          valid_error &= (stream->fts_options & FTS_STOP) != 0;
          valid_error &= stream->fts_child == nullptr;
          valid_error &= entry->fts_errno == ENOMEM;
          valid_error &= !LIBC_NAMESPACE::strcmp(before, entry->fts_path);
          break;
        }
      }
    }
    int error = libc_errno;
    completed = !failed && stream && error == 0;
    if (stream)
      walk_close(stream);
    tracking = false;
    EXPECT_TRUE(valid_error);
    EXPECT_EQ(error, failed ? ENOMEM : 0);
    EXPECT_EQ(live, size_t(0));
    EXPECT_EQ(descriptor_count(), descriptors);
  }
  EXPECT_TRUE(completed);
}

TEST_F(LlvmLibcFtsNftwTest, UnreadablePreparationAdvancesWithoutPostorder) {
  // This real-permission observer requires unprivileged execution. A root run
  // does not qualify denial behavior; use an unprivileged fixture at admission.
  if (LIBC_NAMESPACE::syscall_impl<long>(SYS_getuid) == 0)
    return;
  directory("denied");
  char name[4096];
  path(name, "denied");
  RestoreMode mode{name};
  WalkOwner owner{open(name)};
  ASSERT_NE(owner.stream, static_cast<FTS *>(nullptr));
  FTSENT *entry = walk_read(owner.stream, WalkPolicy::Nftw);
  ASSERT_NE(entry, static_cast<FTSENT *>(nullptr));
  ASSERT_EQ(LIBC_NAMESPACE::chmod(name, 0000), 0);
  EXPECT_EQ(int(walk_prepare_directory(owner.stream, WalkPolicy::Nftw)),
            int(PrepareResult::Unreadable));
  EXPECT_EQ(int(libc_errno), EACCES);
  EXPECT_EQ(int(entry->fts_info), FTS_DNR);
  EXPECT_EQ(entry->fts_errno, EACCES);
  EXPECT_STREQ(entry->fts_path, name);
  EXPECT_EQ(owner.stream->fts_options & FTS_STOP, 0);
  EXPECT_EQ(walk_read(owner.stream, WalkPolicy::Nftw),
            static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(int(libc_errno), 0);
}

TEST_F(LlvmLibcFtsNftwTest, SearchDenialIsNsNotDanglingLink) {
  if (LIBC_NAMESPACE::syscall_impl<long>(SYS_getuid) == 0)
    return;
  directory("denied");
  file("denied/file");
  link("denied/file", "link");
  char denied[4096], name[4096];
  path(denied, "denied");
  path(name, "link");
  RestoreMode mode{denied};
  ASSERT_EQ(LIBC_NAMESPACE::chmod(denied, 0000), 0);
  WalkOwner owner{open(name, LOGICAL)};
  ASSERT_NE(owner.stream, static_cast<FTS *>(nullptr));
  FTSENT *entry = walk_read(owner.stream, WalkPolicy::Nftw);
  ASSERT_NE(entry, static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(int(entry->fts_info), FTS_NS);
  EXPECT_EQ(entry->fts_errno, EACCES);
  // FTW_NS metadata is undefined; deliberately do not inspect fts_statp here.
  WalkOwner ordinary{open(name, LOGICAL, WalkPolicy::Fts)};
  ASSERT_NE(ordinary.stream, static_cast<FTS *>(nullptr));
  FTSENT *legacy = walk_read(ordinary.stream);
  ASSERT_NE(legacy, static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(int(legacy->fts_info), FTS_SLNONE);
}
