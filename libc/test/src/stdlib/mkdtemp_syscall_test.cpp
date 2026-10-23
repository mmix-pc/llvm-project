//===-- Tests for temporary directory failure boundaries -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/getrandom.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/mkdir.h"
#include "src/stdlib/mkdtemp.h"
#include "src/string/strlen.h"
#include "src/sys/stat/stat.h"
#include "src/unistd/rmdir.h"
#include "test/UnitTest/Test.h"

namespace {
unsigned random_calls, mkdir_calls, collisions;
int random_error, mkdir_error;
bool short_reads, first_interrupt, zero_read, real_directories;
void reset() {
  random_calls = mkdir_calls = collisions = 0;
  random_error = mkdir_error = 0;
  short_reads = first_interrupt = zero_read = real_directories = false;
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
namespace directory_test_syscalls {
ErrorOr<ssize_t> getrandom(void *buffer, size_t length, unsigned int flags) {
  EXPECT_EQ(flags, 0U);
  ++random_calls;
  if (first_interrupt && random_calls == 1)
    return Error(EINTR);
  if (random_error)
    return Error(random_error);
  if (zero_read)
    return ssize_t(0);
  size_t count = short_reads ? size_t(1) : length;
  auto *bytes = static_cast<unsigned char *>(buffer);
  for (size_t i = 0; i < count; ++i)
    bytes[i] = mkdir_calls ? 1 : 0;
  return static_cast<ssize_t>(count);
}
ErrorOr<int> mkdir(const char *path, mode_t mode) {
  ++mkdir_calls;
  EXPECT_EQ(mode, mode_t(0700));
  if (mkdir_calls <= collisions)
    return Error(EEXIST);
  if (mkdir_error)
    return Error(mkdir_error);
  return real_directories ? linux_syscalls::mkdir(path, mode) : ErrorOr<int>(0);
}
} // namespace directory_test_syscalls
} // namespace LIBC_NAMESPACE_DECL

// Keep the provider loop; inject only entropy and mkdir transport.
#define linux_syscalls directory_test_syscalls
#define mkdtemp tested_mkdtemp
#include "src/stdlib/mkdtemp.cpp"
#undef mkdtemp
#undef linux_syscalls

TEST(LlvmLibcMkdtempSyscallTest, InterruptedAndShortEntropyReads) {
  reset();
  first_interrupt = short_reads = true;
  char path[] = "prefixXXXXXXXXXX";
  EXPECT_EQ(LIBC_NAMESPACE::tested_mkdtemp(path), path);
  EXPECT_STREQ(path, "prefixXXXXAAAAAA");
  EXPECT_EQ(random_calls, 7U);
  EXPECT_EQ(mkdir_calls, 1U);
}

TEST(LlvmLibcMkdtempSyscallTest, EntropyErrorsDoNotCreateDirectories) {
  const int errors[] = {EIO, EAGAIN, ENOSYS};
  for (int error : errors) {
    reset();
    random_error = error;
    char path[] = "prefixXXXXXX";
    EXPECT_EQ(LIBC_NAMESPACE::tested_mkdtemp(path),
              static_cast<char *>(nullptr));
    EXPECT_EQ(int(libc_errno), error);
    EXPECT_EQ(random_calls, 1U);
    EXPECT_EQ(mkdir_calls, 0U);
    EXPECT_STREQ(path, "prefixXXXXXX");
  }
  reset();
  zero_read = true;
  char path[] = "XXXXXX";
  EXPECT_EQ(LIBC_NAMESPACE::tested_mkdtemp(path), static_cast<char *>(nullptr));
  EXPECT_EQ(int(libc_errno), EIO);
  EXPECT_EQ(mkdir_calls, 0U);
}

TEST(LlvmLibcMkdtempSyscallTest, DirectoryFailuresAndCollisionLimit) {
  const int errors[] = {EACCES, EROFS, ENOSPC, ENOTDIR};
  for (int error : errors) {
    reset();
    mkdir_error = error;
    char path[] = "XXXXXX";
    EXPECT_EQ(LIBC_NAMESPACE::tested_mkdtemp(path),
              static_cast<char *>(nullptr));
    EXPECT_EQ(int(libc_errno), error);
    EXPECT_EQ(mkdir_calls, 1U);
  }
  reset();
  collisions = 256;
  char path[] = "XXXXXX";
  EXPECT_EQ(LIBC_NAMESPACE::tested_mkdtemp(path), static_cast<char *>(nullptr));
  EXPECT_EQ(int(libc_errno), EEXIST);
  EXPECT_EQ(mkdir_calls, 256U);
  EXPECT_EQ(random_calls, 256U);
}

TEST(LlvmLibcMkdtempSyscallTest, ExistingDirectorySurvivesCollision) {
  reset();
  real_directories = true;
  char existing[1024], pattern[1024];
  const char *prefix = libc_make_test_file_path("mkdir_collision_");
  size_t length = LIBC_NAMESPACE::strlen(prefix);
  ASSERT_LT(length + 7, sizeof(existing));
  __builtin_memcpy(existing, prefix, length);
  __builtin_memcpy(existing + length, "AAAAAA", 7);
  __builtin_memcpy(pattern, prefix, length);
  __builtin_memcpy(pattern + length, "XXXXXX", 7);
  ASSERT_TRUE(
      LIBC_NAMESPACE::linux_syscalls::mkdir(existing, 0711).has_value());
  struct stat before, after, created;
  ASSERT_EQ(LIBC_NAMESPACE::stat(existing, &before), 0);
  ASSERT_EQ(LIBC_NAMESPACE::tested_mkdtemp(pattern), pattern);
  EXPECT_EQ(mkdir_calls, 2U);
  EXPECT_EQ(random_calls, 2U);
  ASSERT_EQ(LIBC_NAMESPACE::stat(existing, &after), 0);
  ASSERT_EQ(LIBC_NAMESPACE::stat(pattern, &created), 0);
  EXPECT_EQ(before.st_dev, after.st_dev);
  EXPECT_EQ(before.st_ino, after.st_ino);
  EXPECT_EQ(before.st_mode, after.st_mode);
  EXPECT_NE(created.st_ino, before.st_ino);
  EXPECT_EQ(LIBC_NAMESPACE::rmdir(pattern), 0);
  EXPECT_EQ(LIBC_NAMESPACE::rmdir(existing), 0);
}
