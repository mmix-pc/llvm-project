//===-- Directory positioning, scanning and bulk reading tests
//-------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/libc_errno.h"
#include "src/dirent/closedir.h"
#include "src/dirent/dirfd.h"
#include "src/dirent/fdopendir.h"
#include "src/dirent/opendir.h"
#include "src/dirent/readdir.h"
#include "src/dirent/readdir_r.h"
#include "src/dirent/rewinddir.h"
#include "src/dirent/seekdir.h"
#include "src/dirent/telldir.h"
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
