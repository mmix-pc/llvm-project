//===-- Directory positioning, scanning and bulk reading tests
//-------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/libc_errno.h"
#include "src/dirent/alphasort.h"
#include "src/dirent/closedir.h"
#include "src/dirent/dirfd.h"
#include "src/dirent/fdopendir.h"
#include "src/dirent/opendir.h"
#include "src/dirent/posix_getdents.h"
#include "src/dirent/readdir.h"
#include "src/dirent/readdir_r.h"
#include "src/dirent/rewinddir.h"
#include "src/dirent/scandir.h"
#include "src/dirent/scandirat.h"
#include "src/dirent/seekdir.h"
#include "src/dirent/telldir.h"
#include "src/dirent/versionsort.h"
#include "src/fcntl/open.h"
#include "src/string/strcpy.h"
#include "src/unistd/close.h"
#include "src/unistd/lseek.h"
#include "test/UnitTest/Test.h"

#include "hdr/errno_macros.h"
#include <dirent.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

TEST(LlvmLibcDirectoryOperations, PositionsAndReentrantRead) {
  DIR *dir = LIBC_NAMESPACE::opendir("testdata");
  ASSERT_NE(dir, static_cast<DIR *>(nullptr));
  EXPECT_EQ(LIBC_NAMESPACE::telldir(dir), 0L);
  ASSERT_NE(LIBC_NAMESPACE::readdir(dir), static_cast<dirent *>(nullptr));
  long saved = LIBC_NAMESPACE::telldir(dir);
  ASSERT_GE(saved, 0L);
  dirent *next = LIBC_NAMESPACE::readdir(dir);
  ASSERT_NE(next, static_cast<dirent *>(nullptr));
  char name[256];
  LIBC_NAMESPACE::strcpy(name, next->d_name);
  LIBC_NAMESPACE::seekdir(dir, saved);
  next = LIBC_NAMESPACE::readdir(dir);
  ASSERT_NE(next, static_cast<dirent *>(nullptr));
  EXPECT_STREQ(next->d_name, name);
  LIBC_NAMESPACE::seekdir(dir, saved);
  alignas(dirent) unsigned char storage[offsetof(dirent, d_name) + 256];
  auto *entry = reinterpret_cast<dirent *>(storage);
  dirent *result = nullptr;
  LIBC_NAMESPACE::libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::readdir_r(dir, entry, &result), 0);
  EXPECT_EQ(result, entry);
  EXPECT_STREQ(entry->d_name, name);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  do {
    ASSERT_EQ(LIBC_NAMESPACE::readdir_r(dir, entry, &result), 0);
  } while (result);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  LIBC_NAMESPACE::rewinddir(dir);
  EXPECT_EQ(LIBC_NAMESPACE::telldir(dir), 0L);
  EXPECT_NE(LIBC_NAMESPACE::readdir(dir), static_cast<dirent *>(nullptr));
  EXPECT_EQ(LIBC_NAMESPACE::closedir(dir), 0);
  LIBC_NAMESPACE::libc_errno = 0;
}

TEST(LlvmLibcDirectoryOperations, RefillAndMaximumName) {
  DIR *dir = LIBC_NAMESPACE::opendir("testdata/positions");
  ASSERT_NE(dir, static_cast<DIR *>(nullptr));
  long positions[163];
  char names[163][256];
  unsigned count = 0;
  bool long_name = false;
  for (;;) {
    long before = LIBC_NAMESPACE::telldir(dir);
    // The extra byte must not be overwritten with kernel record padding.
    alignas(dirent) unsigned char storage[offsetof(dirent, d_name) + 257] = {};
    storage[sizeof(storage) - 1] = 0x5a;
    auto *entry = reinterpret_cast<dirent *>(storage);
    dirent *next = nullptr;
    ASSERT_EQ(LIBC_NAMESPACE::readdir_r(dir, entry, &next), 0);
    if (!next)
      break;
    ASSERT_LT(count, 163U);
    positions[count] = before;
    LIBC_NAMESPACE::strcpy(names[count++], next->d_name);
    if (next->d_name[0] == 'x') {
      long_name = true;
      EXPECT_EQ(next->d_name[254], 'x');
      EXPECT_EQ(next->d_name[255], '\0');
      EXPECT_EQ(storage[sizeof(storage) - 1], static_cast<unsigned char>(0x5a));
    }
  }
  EXPECT_EQ(count, 163U);
  EXPECT_TRUE(long_name);
  // Replay cookies after EOF, including entries outside the current buffer.
  while (count) {
    --count;
    LIBC_NAMESPACE::seekdir(dir, positions[count]);
    dirent *entry = LIBC_NAMESPACE::readdir(dir);
    ASSERT_NE(entry, static_cast<dirent *>(nullptr));
    EXPECT_STREQ(entry->d_name, names[count]);
  }
  EXPECT_EQ(LIBC_NAMESPACE::closedir(dir), 0);
}

static int files_only(const dirent *entry) {
  // A callback may change errno without making directory enumeration fail.
  LIBC_NAMESPACE::libc_errno = EINVAL;
  return entry->d_name[0] == 'f';
}
static int exclude_all(const dirent *) { return 0; }
static void free_list(dirent **list, int count) {
  for (int i = 0; i < count; ++i)
    free(list[i]);
  free(list);
}

TEST(LlvmLibcDirectoryOperations, ScanAndSort) {
  dirent **list = nullptr;
  LIBC_NAMESPACE::libc_errno = EDOM;
  int count = LIBC_NAMESPACE::scandir("testdata", &list, files_only,
                                      LIBC_NAMESPACE::alphasort);
  ASSERT_EQ(count, 2);
  EXPECT_STREQ(list[0]->d_name, "file1.txt");
  EXPECT_STREQ(list[1]->d_name, "file2.txt");
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  free_list(list, count);
  list = nullptr;
  count = LIBC_NAMESPACE::scandir("testdata", &list, exclude_all, nullptr);
  EXPECT_EQ(count, 0);
  free_list(list, count);
  int fd = LIBC_NAMESPACE::open("testdata", O_RDONLY | O_DIRECTORY);
  ASSERT_GE(fd, 0);
  count = LIBC_NAMESPACE::scandirat(fd, ".", &list, files_only,
                                    LIBC_NAMESPACE::versionsort);
  ASSERT_EQ(count, 2);
  free_list(list, count);
  count = LIBC_NAMESPACE::scandirat(fd, ".", &list, files_only, nullptr);
  ASSERT_EQ(count, 2);
  free_list(list, count);
  EXPECT_EQ(LIBC_NAMESPACE::close(fd), 0);
  LIBC_NAMESPACE::libc_errno = 0;
}

TEST(LlvmLibcDirectoryOperations, VersionAndCollationOrder) {
  alignas(dirent) char a_storage[sizeof(dirent) + 32] = {};
  alignas(dirent) char b_storage[sizeof(dirent) + 32] = {};
  auto *a = reinterpret_cast<dirent *>(a_storage);
  auto *b = reinterpret_cast<dirent *>(b_storage);
  LIBC_NAMESPACE::strcpy(a->d_name, "file2");
  LIBC_NAMESPACE::strcpy(b->d_name, "file10");
  const dirent *left = a, *right = b;
  EXPECT_GT(LIBC_NAMESPACE::alphasort(&left, &right), 0);
  EXPECT_LT(LIBC_NAMESPACE::versionsort(&left, &right), 0);
  LIBC_NAMESPACE::strcpy(a->d_name, "file01");
  LIBC_NAMESPACE::strcpy(b->d_name, "file1");
  EXPECT_LT(LIBC_NAMESPACE::versionsort(&left, &right), 0);
}

TEST(LlvmLibcDirectoryOperations, ScanErrorsDoNotPublish) {
  dirent *sentinel = nullptr;
  dirent **list = &sentinel;
  EXPECT_EQ(LIBC_NAMESPACE::scandir("testdata/absent", &list, nullptr, nullptr),
            -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOENT);
  EXPECT_EQ(list, &sentinel);
  EXPECT_EQ(
      LIBC_NAMESPACE::scandir("testdata/file1.txt", &list, nullptr, nullptr),
      -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOTDIR);
  EXPECT_EQ(LIBC_NAMESPACE::scandirat(-1, ".", &list, nullptr, nullptr), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EBADF);
  LIBC_NAMESPACE::libc_errno = 0;
}

TEST(LlvmLibcDirectoryOperations, BulkReadAndFdPosition) {
  int fd = LIBC_NAMESPACE::open("testdata", O_RDONLY | O_DIRECTORY);
  ASSERT_GE(fd, 0);
  alignas(dirent) unsigned char buffer[4096];
  auto bytes = LIBC_NAMESPACE::posix_getdents(fd, buffer, sizeof(buffer), 0);
  ASSERT_GT(bytes, ssize_t(0));
  size_t position = 0, count = 0;
  while (position < size_t(bytes)) {
    auto *entry = reinterpret_cast<dirent *>(buffer + position);
    ASSERT_GT(size_t(entry->d_reclen), offsetof(dirent, d_name));
    ASSERT_LE(position + entry->d_reclen, size_t(bytes));
    EXPECT_EQ(size_t(entry->d_reclen) % alignof(dirent), size_t(0));
    ++count;
    position += entry->d_reclen;
  }
  EXPECT_GE(count, size_t(6));
  off_t offset = LIBC_NAMESPACE::lseek(fd, 0, SEEK_CUR);
  ASSERT_GT(offset, off_t(0));
  DIR *dir = LIBC_NAMESPACE::fdopendir(fd);
  ASSERT_NE(dir, static_cast<DIR *>(nullptr));
  EXPECT_EQ(LIBC_NAMESPACE::telldir(dir), long(offset));
  LIBC_NAMESPACE::libc_errno = EDOM;
  EXPECT_EQ(LIBC_NAMESPACE::posix_getdents(fd, buffer, sizeof(buffer), 0),
            ssize_t(0));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  LIBC_NAMESPACE::rewinddir(dir);
  EXPECT_EQ(LIBC_NAMESPACE::posix_getdents(fd, buffer, 1, 0), ssize_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EINVAL);
  EXPECT_EQ(LIBC_NAMESPACE::posix_getdents(fd, buffer, sizeof(buffer), 1),
            ssize_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EOPNOTSUPP);
  EXPECT_EQ(LIBC_NAMESPACE::posix_getdents(-1, buffer, sizeof(buffer), 0),
            ssize_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EBADF);
  EXPECT_EQ(LIBC_NAMESPACE::closedir(dir), 0);
  fd = LIBC_NAMESPACE::open("testdata/file1.txt", O_RDONLY);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(LIBC_NAMESPACE::posix_getdents(fd, buffer, sizeof(buffer), 0),
            ssize_t(-1));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOTDIR);
  EXPECT_EQ(LIBC_NAMESPACE::close(fd), 0);
  LIBC_NAMESPACE::libc_errno = 0;
}
