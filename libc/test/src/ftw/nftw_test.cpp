//===-- Real filesystem tests for ftw and nftw ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Link the real ftw/nftw/walk/FTS objects and the providers included below.
// Build this file and all three adapter objects with -fexceptions and
// -funwind-tables. Descriptor observations here cover callback/return states;
// the injection test separately observes phase overlap and transient opens.

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "src/__support/libc_errno.h"
#include "src/fcntl/fcntl.h"
#include "src/fcntl/open.h"
#include "src/fts/engine.h"
#include "src/ftw/ftw.h"
#include "src/ftw/nftw.h"
#include "src/stdio/rename.h"
#include "src/stdio/snprintf.h"
#include "src/stdlib/mkdtemp.h"
#include "src/string/strcmp.h"
#include "src/string/strcpy.h"
#include "src/string/strlen.h"
#include "src/sys/stat/mkdir.h"
#include "src/sys/stat/mkfifo.h"
#include "src/sys/stat/stat.h"
#include "src/unistd/chdir.h"
#include "src/unistd/close.h"
#include "src/unistd/getcwd.h"
#include "src/unistd/rmdir.h"
#include "src/unistd/symlink.h"
#include "src/unistd/unlink.h"
#include "test/UnitTest/Test.h"
#include <ftw.h>

#ifndef __cpp_exceptions
#error "nftw callback ownership tests require exceptions enabled"
#endif

namespace {
struct Seen {
  char path[512];
  int type;
  int base;
  int level;
  mode_t mode;
};

struct Context {
  const char *original_cwd;
  bool chdir = false;
  bool descriptors[256]{};
  Seen seen[64]{};
  int count = 0;
  int throw_at = 0;
} context;

void snapshot_descriptors() {
  for (int fd = 0; fd < 256; ++fd)
    context.descriptors[fd] = LIBC_NAMESPACE::fcntl(fd, F_GETFD) >= 0;
}

void expect_descriptors(int extra) {
  int added = 0;
  for (int fd = 0; fd < 256; ++fd) {
    int flags = LIBC_NAMESPACE::fcntl(fd, F_GETFD);
    if (context.descriptors[fd]) {
      EXPECT_GE(flags, 0);
    } else if (flags >= 0) {
      ++added;
      EXPECT_TRUE((flags & FD_CLOEXEC) != 0);
    }
  }
  EXPECT_EQ(added, extra);
}

int record(const char *path, const struct stat *status, int type,
           FTW *position) {
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), 0);
  if (context.count == 64 || LIBC_NAMESPACE::strlen(path) >= 512) {
    LIBC_NAMESPACE::libc_errno = EOVERFLOW;
    return -1;
  }
  Seen &seen = context.seen[context.count++];
  LIBC_NAMESPACE::strcpy(seen.path, path);
  seen.type = type;
  seen.base = position ? position->base : -1;
  seen.level = position ? position->level : -1;
  if (type != FTW_NS)
    seen.mode = status->st_mode;

  char expected[4096];
  LIBC_NAMESPACE::strcpy(expected, context.original_cwd);
  if (context.chdir && position) {
    size_t prefix = 0;
    if (*path == '/') {
      LIBC_NAMESPACE::strcpy(expected, path);
    } else {
      prefix = LIBC_NAMESPACE::strlen(expected);
      expected[prefix++] = '/';
      LIBC_NAMESPACE::strcpy(expected + prefix, path);
    }
    size_t length = LIBC_NAMESPACE::strlen(path);
    EXPECT_GE(position->base, 0);
    EXPECT_LE(static_cast<size_t>(position->base), length);
    if (type != FTW_DP && static_cast<size_t>(position->base) != length) {
      if (!position->base)
        LIBC_NAMESPACE::strcpy(expected, context.original_cwd);
      else
        expected[prefix + position->base] = '\0';
    }
  }
  struct stat actual{}, wanted{};
  EXPECT_EQ(LIBC_NAMESPACE::stat(".", &actual), 0);
  EXPECT_EQ(LIBC_NAMESPACE::stat(expected, &wanted), 0);
  EXPECT_EQ(actual.st_dev, wanted.st_dev);
  EXPECT_EQ(actual.st_ino, wanted.st_ino);
  expect_descriptors(context.chdir ? 1 : 0);
  if (context.count == context.throw_at)
    throw 29;
  if (position) {
    position->base = -1;
    position->level = -1;
  }
  LIBC_NAMESPACE::libc_errno = 0;
  return 0;
}

int record_legacy(const char *path, const struct stat *status, int type) {
  return record(path, status, type, nullptr);
}

char nested_path[4096];
int outer_calls, inner_calls;
int inner_callback(const char *, const struct stat *, int type) {
  EXPECT_EQ(type, int(FTW_F));
  ++inner_calls;
  return 0;
}
int outer_callback(const char *, const struct stat *, int, FTW *) {
  if (++outer_calls == 1)
    EXPECT_EQ(LIBC_NAMESPACE::ftw(nested_path, inner_callback, 1), 0);
  return 0;
}

char rename_from[4096], rename_to[4096];
int rename_callback(const char *, const struct stat *, int, FTW *) {
  EXPECT_EQ(LIBC_NAMESPACE::rename(rename_from, rename_to), 0);
  LIBC_NAMESPACE::libc_errno = ENOSPC;
  return 41;
}

class LlvmLibcNftwTest : public LIBC_NAMESPACE::testing::Test {
protected:
  char root[64];
  char original[4096];

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
    int fd = LIBC_NAMESPACE::open(name, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC,
                                  0600);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(LIBC_NAMESPACE::close(fd), 0);
  }
  void link(const char *target, const char *suffix) {
    char name[4096];
    path(name, suffix);
    ASSERT_EQ(LIBC_NAMESPACE::symlink(target, name), 0);
  }
  void tree() {
    directory("dir");
    file("dir/leaf");
    directory("empty");
    file("file");
  }
  void reset(bool chdir = false) {
    context = {};
    context.original_cwd = original;
    context.chdir = chdir;
    snapshot_descriptors();
    LIBC_NAMESPACE::libc_errno = EDOM;
  }
  int find(const char *suffix) {
    char name[4096];
    if (*suffix)
      path(name, suffix);
    else
      LIBC_NAMESPACE::strcpy(name, root);
    for (int i = 0; i < context.count; ++i)
      if (!LIBC_NAMESPACE::strcmp(context.seen[i].path, name))
        return i;
    return -1;
  }
  void expect_original_cwd() {
    char current[4096];
    ASSERT_NE(LIBC_NAMESPACE::getcwd(current, sizeof(current)), nullptr);
    EXPECT_STREQ(current, original);
    expect_descriptors(0);
  }
  void SetUp() override {
    ASSERT_NE(LIBC_NAMESPACE::getcwd(original, sizeof(original)), nullptr);
    LIBC_NAMESPACE::strcpy(root, "nftw-test-XXXXXX");
    ASSERT_NE(LIBC_NAMESPACE::mkdtemp(root), nullptr);
    reset();
  }
  void TearDown() override {
    ASSERT_EQ(LIBC_NAMESPACE::chdir(original), 0);
    char *paths[] = {root, nullptr};
    FTS *stream = LIBC_NAMESPACE::fts::walk_open(
        paths, FTS_PHYSICAL | FTS_NOCHDIR, nullptr);
    ASSERT_NE(stream, nullptr);
    while (FTSENT *entry = LIBC_NAMESPACE::fts::walk_read(stream)) {
      if (entry->fts_info == FTS_DP)
        EXPECT_EQ(LIBC_NAMESPACE::rmdir(entry->fts_accpath), 0);
      else if (entry->fts_info != FTS_D)
        EXPECT_EQ(LIBC_NAMESPACE::unlink(entry->fts_accpath), 0);
    }
    EXPECT_EQ(LIBC_NAMESPACE::fts::walk_close(stream), 0);
    LIBC_NAMESPACE::libc_errno = 0;
  }
};
} // namespace

TEST_F(LlvmLibcNftwTest, OrderMetadataCwdAndDescriptorLimits) {
  tree();
  const int modes[] = {0, FTW_DEPTH, FTW_CHDIR, FTW_CHDIR | FTW_DEPTH};
  const int limits[] = {1, 2, 17};
  for (int flags : modes) {
    for (int limit : limits) {
      reset(flags & FTW_CHDIR);
      ASSERT_EQ(LIBC_NAMESPACE::nftw(root, record, limit, flags | FTW_PHYS), 0);
      ASSERT_EQ(context.count, 5);
      int parent = find("dir");
      int leaf = find("dir/leaf");
      int empty = find("empty");
      ASSERT_GE(parent, 0);
      ASSERT_GE(leaf, 0);
      ASSERT_GE(empty, 0);
      EXPECT_EQ(context.seen[leaf].type, int(FTW_F));
      EXPECT_TRUE(S_ISREG(context.seen[leaf].mode) != 0);
      EXPECT_EQ(context.seen[leaf].level, 2);
      EXPECT_EQ(context.seen[leaf].base,
                static_cast<int>(LIBC_NAMESPACE::strlen(root)) + 5);
      EXPECT_EQ(context.seen[empty].type,
                int(flags & FTW_DEPTH ? FTW_DP : FTW_D));
      if (flags & FTW_DEPTH) {
        EXPECT_LT(leaf, parent);
        EXPECT_EQ(find(""), 4);
      } else {
        EXPECT_LT(parent, leaf);
        EXPECT_EQ(find(""), 0);
      }
      expect_original_cwd();
    }
  }
}

TEST_F(LlvmLibcNftwTest, PhysicalLinksLogicalAliasesAndAncestorCycles) {
  directory("dir");
  file("dir/leaf");
  link("dir", "alias");
  link("missing", "dangling");
  link(".", "cycle");
  ASSERT_EQ(LIBC_NAMESPACE::nftw(root, record, 1, FTW_PHYS), 0);
  const char *links[] = {"alias", "dangling", "cycle"};
  for (const char *name : links) {
    int index = find(name);
    ASSERT_GE(index, 0);
    EXPECT_EQ(context.seen[index].type, int(FTW_SL));
    EXPECT_TRUE(S_ISLNK(context.seen[index].mode) != 0);
  }
  EXPECT_EQ(find("alias/leaf"), -1);
  reset();
  ASSERT_EQ(LIBC_NAMESPACE::nftw(root, record, 1, 0), 0);
  EXPECT_GE(find("dir/leaf"), 0);
  EXPECT_GE(find("alias/leaf"), 0);
  int dangling = find("dangling");
  int cycle = find("cycle");
  ASSERT_GE(dangling, 0);
  ASSERT_GE(cycle, 0);
  EXPECT_EQ(context.seen[dangling].type, int(FTW_SLN));
  EXPECT_TRUE(S_ISLNK(context.seen[dangling].mode) != 0);
  EXPECT_EQ(context.seen[cycle].type, int(FTW_D));
  EXPECT_EQ(find("cycle/dir"), -1);
  reset(true);
  ASSERT_EQ(LIBC_NAMESPACE::nftw(root, record, 1, FTW_DEPTH | FTW_CHDIR), 0);
  EXPECT_EQ(find("cycle"), -1);
  EXPECT_GE(find("alias/leaf"), 0);
  expect_original_cwd();
}

TEST_F(LlvmLibcNftwTest, TraditionalFtwReportsDanglingLinkAsNs) {
  link("missing", "dangling");
  char name[4096];
  path(name, "dangling");
  ASSERT_EQ(LIBC_NAMESPACE::nftw(name, record, 1, 0), 0);
  ASSERT_EQ(context.count, 1);
  EXPECT_EQ(context.seen[0].type, int(FTW_SLN));
  EXPECT_TRUE(S_ISLNK(context.seen[0].mode) != 0);
  reset();
  ASSERT_EQ(LIBC_NAMESPACE::ftw(name, record_legacy, 1), 0);
  ASSERT_EQ(context.count, 1);
  EXPECT_EQ(context.seen[0].type, int(FTW_NS));
}

TEST_F(LlvmLibcNftwTest, RootAndLogicalTargetErrorsAreFatal) {
  file("file");
  link("file/child", "notdir");
  link("loop", "loop");
  struct Case {
    const char *suffix;
    int error;
  } cases[] = {{"missing", ENOENT},
               {"file/child", ENOTDIR},
               {"notdir", ENOTDIR},
               {"loop", ELOOP}};
  for (const Case &item : cases) {
    char name[4096];
    path(name, item.suffix);
    reset();
    EXPECT_EQ(LIBC_NAMESPACE::nftw(name, record, 1, 0), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), item.error);
    EXPECT_EQ(context.count, 0);
    expect_original_cwd();
  }
  reset();
  EXPECT_EQ(LIBC_NAMESPACE::nftw("", record, 1, 0), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOENT);
  EXPECT_EQ(context.count, 0);
}

TEST_F(LlvmLibcNftwTest, TrailingSlashesAndSpecialFiles) {
  char name[4096];
  path(name, "fifo");
  ASSERT_EQ(LIBC_NAMESPACE::mkfifo(name, 0600), 0);
  path(name, "/");
  reset(true);
  ASSERT_EQ(LIBC_NAMESPACE::nftw(name, record, 1, FTW_PHYS | FTW_CHDIR), 0);
  ASSERT_EQ(context.count, 2);
  EXPECT_STREQ(context.seen[0].path, name);
  EXPECT_EQ(context.seen[0].base, 0);
  EXPECT_EQ(context.seen[1].type, int(FTW_F));
  EXPECT_TRUE(S_ISFIFO(context.seen[1].mode) != 0);
  expect_original_cwd();
  file("file");
  path(name, "file/");
  reset();
  EXPECT_EQ(LIBC_NAMESPACE::nftw(name, record, 1, FTW_PHYS), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOTDIR);
}

TEST_F(LlvmLibcNftwTest,
       PhysicalRootSymlinkTrailingSlashStillResolvesDirectory) {
  directory("dir");
  file("dir/leaf");
  link("dir", "alias");
  char name[4096];
  path(name, "alias/");
  reset(true);
  ASSERT_EQ(LIBC_NAMESPACE::nftw(name, record, 1, FTW_PHYS | FTW_CHDIR), 0);
  ASSERT_EQ(context.count, 2);
  EXPECT_STREQ(context.seen[0].path, name);
  EXPECT_EQ(context.seen[0].type, int(FTW_D));
  EXPECT_EQ(context.seen[1].type, int(FTW_F));
  expect_original_cwd();
}

TEST_F(LlvmLibcNftwTest, AbsoluteRootAndNestedLegacyWalk) {
  tree();
  char absolute[4096];
  LIBC_NAMESPACE::snprintf(absolute, sizeof(absolute), "%s/%s", original, root);
  reset(true);
  ASSERT_EQ(LIBC_NAMESPACE::nftw(absolute, record, 1, FTW_CHDIR | FTW_DEPTH),
            0);
  EXPECT_EQ(context.count, 5);
  expect_original_cwd();
  path(nested_path, "file");
  outer_calls = inner_calls = 0;
  ASSERT_EQ(LIBC_NAMESPACE::nftw(root, outer_callback, 1, 0), 0);
  EXPECT_EQ(outer_calls, 5);
  EXPECT_EQ(inner_calls, 1);
}

TEST_F(LlvmLibcNftwTest, ExceptionClosesPreparedWalkAndRestoresCwd) {
  tree();
  reset(true);
  context.throw_at = 2;
  bool caught = false;
  try {
    LIBC_NAMESPACE::nftw(root, record, 1, FTW_CHDIR);
  } catch (int value) {
    EXPECT_EQ(value, 29);
    caught = true;
  }
  EXPECT_TRUE(caught);
  EXPECT_EQ(context.count, 2);
  expect_original_cwd();
}

TEST_F(LlvmLibcNftwTest, CallbackAnchorSurvivesOriginalCwdRename) {
  directory("cwd");
  directory("cwd/tree");
  LIBC_NAMESPACE::snprintf(rename_from, sizeof(rename_from), "%s/%s/cwd",
                           original, root);
  LIBC_NAMESPACE::snprintf(rename_to, sizeof(rename_to), "%s/%s/moved",
                           original, root);
  ASSERT_EQ(LIBC_NAMESPACE::chdir(rename_from), 0);
  struct stat before{}, after{};
  ASSERT_EQ(LIBC_NAMESPACE::stat(".", &before), 0);
  EXPECT_EQ(
      LIBC_NAMESPACE::nftw("tree", rename_callback, 1, FTW_CHDIR | FTW_DEPTH),
      41);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOSPC);
  ASSERT_EQ(LIBC_NAMESPACE::stat(".", &after), 0);
  EXPECT_EQ(before.st_dev, after.st_dev);
  EXPECT_EQ(before.st_ino, after.st_ino);
  ASSERT_EQ(LIBC_NAMESPACE::chdir(original), 0);
  expect_original_cwd();
}
