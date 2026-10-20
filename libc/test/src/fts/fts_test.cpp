//===-- Tests for file hierarchy traversal --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/libc_errno.h"
#include "src/fts/fts_children.h"
#include "src/fts/fts_close.h"
#include "src/fts/fts_open.h"
#include "src/fts/fts_read.h"
#include "src/fts/fts_set.h"
#include "src/fts/linux_io.h"
#include "test/UnitTest/Test.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Inject allocation failures into the production engine, not a model walker.
static bool inject = false;
static int fail_after = -1;
static void *owned[4096];
static size_t live = 0;
extern "C" void *__real_malloc(size_t);
extern "C" void *__real_realloc(void *, size_t);
extern "C" void __real_free(void *);
static void forget(void *p) {
  for (size_t i = 0; i < live; ++i)
    if (owned[i] == p) {
      owned[i] = owned[--live];
      return;
    }
}
extern "C" void *__wrap_malloc(size_t size) {
  if (inject && fail_after-- == 0)
    return nullptr;
  void *p = __real_malloc(size);
  if (inject && p)
    owned[live++] = p;
  return p;
}
extern "C" void *__wrap_realloc(void *old, size_t size) {
  if (inject && fail_after-- == 0)
    return nullptr;
  void *p = __real_realloc(old, size);
  if (inject && p) {
    forget(old);
    owned[live++] = p;
  }
  return p;
}
extern "C" void __wrap_free(void *p) {
  if (inject)
    forget(p);
  __real_free(p);
}

#include "src/fcntl/fcntl.h"
#include "src/fcntl/open.h"
#include "src/stdio/snprintf.h"
#include "src/stdlib/mkdtemp.h"
#include "src/string/strcmp.h"
#include "src/string/strcpy.h"
#include "src/string/strlen.h"
#include "src/sys/stat/chmod.h"
#include "src/sys/stat/mkdir.h"
#include "src/unistd/chdir.h"
#include "src/unistd/close.h"
#include "src/unistd/getcwd.h"
#include "src/unistd/rmdir.h"
#include "src/unistd/symlink.h"
#include "src/unistd/unlink.h"

namespace {
// Filesystem setup and traversal use libc providers on the native kernel.
// The target fixture separately checks the installed public interface.
class LlvmLibcFtsTest : public LIBC_NAMESPACE::testing::Test {
protected:
  char root[64];
  char cwd[4096];
  void path(char *out, const char *suffix) {
    LIBC_NAMESPACE::snprintf(out, 4096, "%s/%s", root, suffix);
  }
  void file(const char *suffix) {
    char name[4096];
    path(name, suffix);
    int fd = LIBC_NAMESPACE::open(name, O_CREAT | O_WRONLY, 0600);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(LIBC_NAMESPACE::close(fd), 0);
  }
  void directory(const char *suffix) {
    char name[4096];
    path(name, suffix);
    ASSERT_EQ(LIBC_NAMESPACE::mkdir(name, 0700), 0);
  }
  void link(const char *target, const char *suffix) {
    char name[4096];
    path(name, suffix);
    ASSERT_EQ(LIBC_NAMESPACE::symlink(target, name), 0);
  }
  FTS *open(int options = FTS_PHYSICAL | FTS_NOCHDIR,
            __fts_compare_t compare = nullptr) {
    char *paths[] = {root, nullptr};
    return LIBC_NAMESPACE::fts_open(paths, options, compare);
  }
  static int compare(const FTSENT **a, const FTSENT **b) {
    return LIBC_NAMESPACE::strcmp((*a)->fts_name, (*b)->fts_name);
  }
  static int descriptor_count() {
    int count = 0;
    for (int fd = 0; fd < 128; ++fd)
      count += LIBC_NAMESPACE::fcntl(fd, F_GETFD) >= 0;
    return count;
  }
  void SetUp() override {
    ASSERT_NE(LIBC_NAMESPACE::getcwd(cwd, sizeof(cwd)),
              static_cast<char *>(nullptr));
    LIBC_NAMESPACE::strcpy(root, "fts-test-XXXXXX");
    ASSERT_NE(LIBC_NAMESPACE::mkdtemp(root), static_cast<char *>(nullptr));
    directory("a");
    file("a/file");
    directory("b");
    file("z");
    link("a", "link");
    link("missing", "dangling");
    link("..", "a/cycle");
  }
  void TearDown() override {
    ASSERT_EQ(LIBC_NAMESPACE::chdir(cwd), 0);
    char *paths[] = {root, nullptr};
    FTS *stream =
        LIBC_NAMESPACE::fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, nullptr);
    ASSERT_NE(stream, static_cast<FTS *>(nullptr));
    while (FTSENT *e = LIBC_NAMESPACE::fts_read(stream)) {
      if (e->fts_info == FTS_DP)
        ASSERT_EQ(LIBC_NAMESPACE::rmdir(e->fts_accpath), 0);
      else if (e->fts_info != FTS_D) {
        int result = LIBC_NAMESPACE::unlink(e->fts_accpath);
        if (result)
          LIBC_NAMESPACE::testing::tlog << e->fts_accpath << " info "
                                        << int(e->fts_info) << " node errno "
                                        << e->fts_errno << "\n";
        ASSERT_EQ(result, 0);
      }
    }
    ASSERT_EQ(LIBC_NAMESPACE::fts_close(stream), 0);
  }
};
} // namespace

TEST_F(LlvmLibcFtsTest, PhysicalSortedRestoreconWalk) {
  FTS *s = open(FTS_PHYSICAL | FTS_NOCHDIR, compare);
  ASSERT_NE(s, static_cast<FTS *>(nullptr));
  int dirs = 0, posts = 0, files = 0, links = 0;
  while (FTSENT *e = LIBC_NAMESPACE::fts_read(s)) {
    ASSERT_NE(e->fts_statp, static_cast<struct stat *>(nullptr));
    ASSERT_EQ(e->fts_pathlen, LIBC_NAMESPACE::strlen(e->fts_path));
    ASSERT_EQ(e->fts_namelen, LIBC_NAMESPACE::strlen(e->fts_name));
    if (e->fts_info == FTS_D) {
      ++dirs;
      if (!LIBC_NAMESPACE::strcmp(e->fts_name, "a"))
        ASSERT_EQ(LIBC_NAMESPACE::fts_set(s, e, FTS_SKIP), 0);
    } else if (e->fts_info == FTS_DP)
      ++posts;
    else if (e->fts_info == FTS_F)
      ++files;
    else if (e->fts_info == FTS_SL)
      ++links;
    else
      ASSERT_TRUE(false);
  }
  EXPECT_EQ(int(libc_errno), 0);
  EXPECT_EQ(dirs, 3);
  EXPECT_EQ(posts, 3);
  EXPECT_EQ(files, 1);
  EXPECT_EQ(links, 2);
  EXPECT_EQ(LIBC_NAMESPACE::fts_close(s), 0);
  char after[4096];
  ASSERT_NE(LIBC_NAMESPACE::getcwd(after, sizeof(after)),
            static_cast<char *>(nullptr));
  EXPECT_STREQ(cwd, after);
}

TEST_F(LlvmLibcFtsTest, LogicalCyclesAndDanglingLinks) {
  FTS *s = open(FTS_LOGICAL);
  ASSERT_NE(s, static_cast<FTS *>(nullptr));
  int cycles = 0, dangling = 0, files = 0;
  while (FTSENT *e = LIBC_NAMESPACE::fts_read(s)) {
    if (e->fts_info == FTS_DC) {
      ASSERT_NE(e->fts_cycle, static_cast<FTSENT *>(nullptr));
      EXPECT_EQ(e->fts_cycle->fts_level, 0);
      ++cycles;
    }
    dangling += e->fts_info == FTS_SLNONE;
    files += e->fts_info == FTS_F;
  }
  EXPECT_EQ(int(libc_errno), 0);
  EXPECT_EQ(cycles, 2);
  EXPECT_EQ(dangling, 1);
  EXPECT_EQ(files, 3);
  EXPECT_EQ(LIBC_NAMESPACE::fts_close(s), 0);
}

TEST_F(LlvmLibcFtsTest, ChildrenInstructionsAndEndOfStream) {
  FTS *s = open(FTS_PHYSICAL | FTS_NOCHDIR, compare);
  ASSERT_NE(s, static_cast<FTS *>(nullptr));
  ASSERT_NE(LIBC_NAMESPACE::fts_children(s, 0), static_cast<FTSENT *>(nullptr));
  FTSENT *root_entry = LIBC_NAMESPACE::fts_read(s);
  ASSERT_NE(root_entry, static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(LIBC_NAMESPACE::fts_set(s, root_entry, FTS_AGAIN), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fts_read(s), root_entry);
  FTSENT *children = LIBC_NAMESPACE::fts_children(s, FTS_NAMEONLY);
  ASSERT_NE(children, static_cast<FTSENT *>(nullptr));
  EXPECT_STREQ(children->fts_name, "a");
  children = LIBC_NAMESPACE::fts_children(s, 0);
  ASSERT_NE(children, static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(LIBC_NAMESPACE::fts_set(s, children, FTS_SKIP), 0);
  EXPECT_NE(LIBC_NAMESPACE::fts_set(s, root_entry, 999), 0);
  EXPECT_EQ(int(libc_errno), EINVAL);
  bool followed = false;
  while (FTSENT *e = LIBC_NAMESPACE::fts_read(s)) {
    if (!LIBC_NAMESPACE::strcmp(e->fts_name, "link") && e->fts_info == FTS_SL) {
      EXPECT_EQ(LIBC_NAMESPACE::fts_set(s, e, FTS_FOLLOW), 0);
      EXPECT_EQ(LIBC_NAMESPACE::fts_read(s), e);
      EXPECT_EQ(int(e->fts_info), FTS_D);
      followed = true;
    }
  }
  EXPECT_TRUE(followed);
  EXPECT_EQ(int(libc_errno), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fts_children(s, 0), static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(LIBC_NAMESPACE::fts_close(s), 0);
}

TEST_F(LlvmLibcFtsTest, ChangeDirectoryAndEarlyClose) {
  FTS *s = open(FTS_PHYSICAL, compare);
  ASSERT_NE(s, static_cast<FTS *>(nullptr));
  ASSERT_NE(LIBC_NAMESPACE::fts_read(s), static_cast<FTSENT *>(nullptr));
  ASSERT_NE(LIBC_NAMESPACE::fts_children(s, 0), static_cast<FTSENT *>(nullptr));
  for (int i = 0; i < 3; ++i)
    ASSERT_NE(LIBC_NAMESPACE::fts_read(s), static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(LIBC_NAMESPACE::fcntl(s->fts_rfd, F_GETFD) & FD_CLOEXEC,
            FD_CLOEXEC);
  EXPECT_EQ(LIBC_NAMESPACE::fts_close(s), 0);
  char after[4096];
  ASSERT_NE(LIBC_NAMESPACE::getcwd(after, sizeof(after)),
            static_cast<char *>(nullptr));
  EXPECT_STREQ(cwd, after);
  s = open();
  ASSERT_NE(s, static_cast<FTS *>(nullptr));
  EXPECT_EQ(LIBC_NAMESPACE::fts_close(s), 0);
}

TEST_F(LlvmLibcFtsTest, NoStatStillDescendsAndDotEntries) {
  FTS *s = open(FTS_PHYSICAL | FTS_NOCHDIR | FTS_NOSTAT | FTS_SEEDOT);
  ASSERT_NE(s, static_cast<FTS *>(nullptr));
  int directories = 0, names = 0;
  while (FTSENT *e = LIBC_NAMESPACE::fts_read(s)) {
    EXPECT_EQ(e->fts_statp, static_cast<struct stat *>(nullptr));
    directories += e->fts_info == FTS_D;
    names += e->fts_info == FTS_NSOK;
  }
  EXPECT_EQ(int(libc_errno), 0);
  EXPECT_EQ(directories, 3);
  EXPECT_GT(names, 0);
  EXPECT_EQ(LIBC_NAMESPACE::fts_close(s), 0);
}

TEST_F(LlvmLibcFtsTest, InvalidOptionsAndMissingRoot) {
  char empty[] = "";
  char missing[] = "fts-does-not-exist";
  char *paths[] = {empty, nullptr};
  EXPECT_EQ(LIBC_NAMESPACE::fts_open(paths, FTS_PHYSICAL, nullptr),
            static_cast<FTS *>(nullptr));
  EXPECT_EQ(int(libc_errno), ENOENT);
  EXPECT_EQ(open(FTS_LOGICAL | FTS_PHYSICAL), static_cast<FTS *>(nullptr));
  EXPECT_EQ(int(libc_errno), EINVAL);
  paths[0] = missing;
  FTS *s = LIBC_NAMESPACE::fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, nullptr);
  ASSERT_NE(s, static_cast<FTS *>(nullptr));
  FTSENT *e = LIBC_NAMESPACE::fts_read(s);
  ASSERT_NE(e, static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(int(e->fts_info), FTS_NS);
  EXPECT_EQ(e->fts_errno, ENOENT);
  EXPECT_EQ(LIBC_NAMESPACE::fts_close(s), 0);
}

TEST_F(LlvmLibcFtsTest, IndependentNoChdirWalks) {
  FTS *a = open(), *b = open();
  ASSERT_NE(a, static_cast<FTS *>(nullptr));
  ASSERT_NE(b, static_cast<FTS *>(nullptr));
  int count = 0;
  while (FTSENT *x = LIBC_NAMESPACE::fts_read(a)) {
    FTSENT *y = LIBC_NAMESPACE::fts_read(b);
    ASSERT_NE(y, static_cast<FTSENT *>(nullptr));
    EXPECT_STREQ(x->fts_path, y->fts_path);
    ++count;
  }
  EXPECT_GT(count, 0);
  EXPECT_EQ(LIBC_NAMESPACE::fts_read(b), static_cast<FTSENT *>(nullptr));
  EXPECT_EQ(LIBC_NAMESPACE::fts_close(a), 0);
  EXPECT_EQ(LIBC_NAMESPACE::fts_close(b), 0);
}

TEST_F(LlvmLibcFtsTest, AllocationFailuresRetireEveryOwnedObject) {
  bool completed = false;
  int descriptors = descriptor_count();
  for (int failure = 0; failure < 200 && !completed; ++failure) {
    fail_after = failure;
    live = 0;
    inject = true;
    FTS *s = open(FTS_PHYSICAL | FTS_NOCHDIR, compare);
    if (s) {
      while (LIBC_NAMESPACE::fts_read(s)) {
      }
      completed = fail_after >= 0;
      LIBC_NAMESPACE::fts_close(s);
    }
    inject = false;
    EXPECT_EQ(live, size_t(0));
    EXPECT_EQ(descriptor_count(), descriptors);
  }
  EXPECT_TRUE(completed);
}

TEST_F(LlvmLibcFtsTest, FollowedAncestorDescriptorRetiredOnEarlyClose) {
  int descriptors = descriptor_count();
  FTS *s = open(FTS_PHYSICAL, compare);
  ASSERT_NE(s, static_cast<FTS *>(nullptr));
  bool inside = false;
  while (FTSENT *e = LIBC_NAMESPACE::fts_read(s)) {
    if (e->fts_info == FTS_SL && !LIBC_NAMESPACE::strcmp(e->fts_name, "link"))
      ASSERT_EQ(LIBC_NAMESPACE::fts_set(s, e, FTS_FOLLOW), 0);
    if (e->fts_info == FTS_F &&
        !LIBC_NAMESPACE::strcmp(e->fts_parent->fts_name, "link")) {
      inside = true;
      EXPECT_EQ(LIBC_NAMESPACE::fcntl(e->fts_parent->fts_symfd, F_GETFD) &
                    FD_CLOEXEC,
                FD_CLOEXEC);
      break;
    }
  }
  EXPECT_TRUE(inside);
  EXPECT_EQ(LIBC_NAMESPACE::fts_close(s), 0);
  EXPECT_EQ(descriptor_count(), descriptors);
}

TEST_F(LlvmLibcFtsTest, DeepPathsAndRepeatedChildren) {
  char suffix[2048] = {};
  for (int i = 0; i < 24; ++i) {
    size_t len = LIBC_NAMESPACE::strlen(suffix);
    LIBC_NAMESPACE::strcpy(suffix + len, len ? "/" : "");
    len = LIBC_NAMESPACE::strlen(suffix);
    for (int j = 0; j < 48; ++j)
      suffix[len + j] = 'x';
    suffix[len + 48] = 0;
    directory(suffix);
  }
  FTS *s = open(FTS_PHYSICAL | FTS_NOCHDIR, compare);
  ASSERT_NE(s, static_cast<FTS *>(nullptr));
  int depth = 0;
  while (FTSENT *e = LIBC_NAMESPACE::fts_read(s)) {
    if (e->fts_level > depth)
      depth = e->fts_level;
    EXPECT_EQ(e->fts_pathlen, LIBC_NAMESPACE::strlen(e->fts_path));
    if (e->fts_info == FTS_D) {
      LIBC_NAMESPACE::fts_children(s, FTS_NAMEONLY);
      LIBC_NAMESPACE::fts_children(s, 0);
    }
  }
  EXPECT_EQ(int(libc_errno), 0);
  EXPECT_EQ(depth, 24);
  EXPECT_EQ(LIBC_NAMESPACE::fts_close(s), 0);
}

TEST_F(LlvmLibcFtsTest, PendingChildrenInstructions) {
  FTS *s = open(FTS_PHYSICAL | FTS_NOCHDIR, compare);
  ASSERT_NE(s, static_cast<FTS *>(nullptr));
  ASSERT_NE(LIBC_NAMESPACE::fts_read(s), static_cast<FTSENT *>(nullptr));
  ASSERT_NE(LIBC_NAMESPACE::fts_children(s, FTS_NAMEONLY),
            static_cast<FTSENT *>(nullptr));
  FTSENT *children = LIBC_NAMESPACE::fts_children(s, 0);
  ASSERT_NE(children, static_cast<FTSENT *>(nullptr));
  for (FTSENT *e = children; e; e = e->fts_link) {
    if (!LIBC_NAMESPACE::strcmp(e->fts_name, "link"))
      ASSERT_EQ(LIBC_NAMESPACE::fts_set(s, e, FTS_FOLLOW), 0);
    else
      ASSERT_EQ(LIBC_NAMESPACE::fts_set(s, e, FTS_SKIP), 0);
  }
  FTSENT *e = LIBC_NAMESPACE::fts_read(s);
  ASSERT_NE(e, static_cast<FTSENT *>(nullptr));
  EXPECT_STREQ(e->fts_name, "link");
  EXPECT_EQ(int(e->fts_info), FTS_D);
  EXPECT_EQ(LIBC_NAMESPACE::fts_close(s), 0);
}

TEST_F(LlvmLibcFtsTest, DirectoryReadErrorsAndMalformedRecords) {
  using namespace LIBC_NAMESPACE::fts;
  Directory d{};
  d.fd = -1;
  EXPECT_EQ(readdir(&d), static_cast<struct dirent *>(nullptr));
  EXPECT_EQ(int(libc_errno), EBADF);
  d.size = 32;
  // A zero record length must not loop forever or advance outside the buffer.
  EXPECT_EQ(readdir(&d), static_cast<struct dirent *>(nullptr));
  EXPECT_EQ(int(libc_errno), EIO);
}

TEST_F(LlvmLibcFtsTest, PermissionErrorIsPerEntry) {
  // Root bypasses directory search permissions.
  if (LIBC_NAMESPACE::syscall_impl<long>(SYS_getuid) == 0)
    return;
  char name[4096];
  path(name, "b");
  ASSERT_EQ(LIBC_NAMESPACE::chmod(name, 0000), 0);
  FTS *s = open();
  ASSERT_NE(s, static_cast<FTS *>(nullptr));
  bool denied = false;
  while (FTSENT *e = LIBC_NAMESPACE::fts_read(s))
    if (e->fts_info == FTS_DNR) {
      EXPECT_STREQ(e->fts_name, "b");
      EXPECT_EQ(e->fts_errno, EACCES);
      denied = true;
    }
  EXPECT_EQ(LIBC_NAMESPACE::fts_close(s), 0);
  EXPECT_EQ(LIBC_NAMESPACE::chmod(name, 0700), 0);
  EXPECT_TRUE(denied);
}
