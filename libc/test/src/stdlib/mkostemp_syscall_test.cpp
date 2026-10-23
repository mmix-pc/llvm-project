//===-- Temporary-file collision and failure boundary tests ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/linux/syscall_wrappers/getrandom.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/libc_errno.h"
#include "src/string/strlen.h"
#include "src/sys/stat/fstat.h"
#include "src/sys/stat/lstat.h"
#include "src/unistd/close.h"
#include "src/unistd/pread.h"
#include "src/unistd/symlink.h"
#include "src/unistd/unlink.h"
#include "src/unistd/write.h"
#include "test/UnitTest/Test.h"

namespace {
int random_error, open_error, collisions, calls, random_calls, seen_flags;
int random_byte = -1;
const char *expected_name = nullptr;
char first_name[7];
bool real_files = false;
} // namespace

namespace LIBC_NAMESPACE_DECL {
namespace temporary_test_syscalls {
ErrorOr<ssize_t> getrandom(void *buffer, size_t length, unsigned int flags) {
  EXPECT_EQ(length, size_t(1));
  EXPECT_EQ(flags, 0U);
  ++random_calls;
  if (random_error)
    return Error(random_error);
  *static_cast<unsigned char *>(buffer) =
      static_cast<unsigned char>(random_byte < 0 ? calls : random_byte);
  return ssize_t(1);
}
ErrorOr<int> open(const char *name, int flags, mode_t mode) {
  ++calls;
  seen_flags = flags;
  EXPECT_EQ(mode, mode_t(0600));
  if (expected_name)
    EXPECT_STREQ(name, expected_name);
  if (calls == 1)
    for (unsigned i = 0; i < 7; ++i)
      first_name[i] = name[i];
  if (collisions-- > 0)
    return Error(EEXIST);
  if (open_error)
    return Error(open_error);
  return real_files ? linux_syscalls::open(name, flags, mode) : ErrorOr<int>(17);
}
} // namespace temporary_test_syscalls
} // namespace LIBC_NAMESPACE_DECL

#define linux_syscalls temporary_test_syscalls
#include "src/stdlib/mkostemp.cpp"
#include "src/stdlib/mkstemp.cpp"
#undef linux_syscalls

TEST(LlvmLibcMkostempSyscallTest, AllXNamesAreValidForBothFileCreators) {
  // Byte 36 selects X in the current helper. This is an implementation-bound
  // entropy input, not a requirement for a particular public random alphabet.
  random_byte = 36;
  const int flags[] = {0, O_CLOEXEC};
  for (int flag : flags) {
    char paths[][11] = {"XXXXXX", "XXXXXXXXXX"};
    for (auto &path : paths) {
      expected_name = path[6] ? "XXXXXXXXXX" : "XXXXXX";
      const int length = path[6] ? 10 : 6;
      calls = random_calls = random_error = open_error = collisions = 0;
      EXPECT_EQ(flag ? LIBC_NAMESPACE::mkostemp(path, flag)
                     : LIBC_NAMESPACE::mkstemp(path),
                17);
      EXPECT_STREQ(path, expected_name);
      EXPECT_EQ(calls, 1);
      EXPECT_EQ(random_calls, length);
      EXPECT_EQ(seen_flags, O_RDWR | O_CREAT | O_EXCL | flag);
    }
  }
  random_byte = -1;
  expected_name = nullptr;
}

TEST(LlvmLibcMkostempSyscallTest, RetriesCollisionWithAtomicFlags) {
  char path[] = "XXXXXX";
  calls = random_calls = random_error = open_error = 0;
  collisions = 1;
  EXPECT_EQ(LIBC_NAMESPACE::mkostemp(path, O_CLOEXEC | O_APPEND), 17);
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(random_calls, 12);
  EXPECT_EQ(seen_flags, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_APPEND);
  EXPECT_STRNE(path, first_name);
}

TEST(LlvmLibcMkostempSyscallTest, PropagatesEntropyAndOpenFailures) {
  const int errors[] = {EINTR, EIO, EMFILE, ENFILE, EACCES, ENOSPC};
  for (int error : errors) {
    char path[] = "XXXXXX";
    calls = random_calls = collisions = open_error = 0;
    random_error = error;
    EXPECT_EQ(LIBC_NAMESPACE::mkostemp(path, 0), -1);
    EXPECT_EQ(int(libc_errno), error);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(random_calls, 1);
    EXPECT_STREQ(path, "XXXXXX");

    random_error = 0;
    open_error = error;
    EXPECT_EQ(LIBC_NAMESPACE::mkostemp(path, 0), -1);
    EXPECT_EQ(int(libc_errno), error);
    EXPECT_EQ(calls, 1);
  }
}

TEST(LlvmLibcMkostempSyscallTest, InvalidInputsHaveNoSyscalls) {
  calls = random_calls = 0;
  char path[] = "XXXXXX";
  EXPECT_EQ(LIBC_NAMESPACE::mkostemp(path, O_DIRECTORY), -1);
  EXPECT_EQ(int(libc_errno), EINVAL);
  char bad[] = "XXXXX";
  EXPECT_EQ(LIBC_NAMESPACE::mkostemp(bad, 0), -1);
  EXPECT_EQ(int(libc_errno), EINVAL);
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(random_calls, 0);
}

TEST(LlvmLibcMkostempSyscallTest, RealCollisionPreservesFileAndSymlinkTarget) {
  // Entropy 0 then 1 selects '-' then '.'; the first real exclusive open
  // must collide. The second must create a different inode without truncation.
  const bool kinds[] = {false, true};
  for (bool symbolic : kinds) {
    char occupied[1024], pattern[1024], target[1024];
    const char *prefix = libc_make_test_file_path("temporary_collision_");
    size_t length = LIBC_NAMESPACE::strlen(prefix);
    ASSERT_LT(length + 7, sizeof(pattern));
    __builtin_memcpy(occupied, prefix, length);
    __builtin_memcpy(occupied + length, "------", 7);
    __builtin_memcpy(pattern, prefix, length);
    __builtin_memcpy(pattern + length, "XXXXXX", 7);
    __builtin_memcpy(target, prefix, length);
    __builtin_memcpy(target + length, "target", 7);
    auto original = LIBC_NAMESPACE::linux_syscalls::open(
        symbolic ? target : occupied, O_CREAT | O_EXCL | O_RDWR, 0640);
    ASSERT_TRUE(original.has_value());
    int fd = *original;
    ASSERT_EQ(LIBC_NAMESPACE::write(fd, "preserved", 9), ssize_t(9));
    if (symbolic)
      ASSERT_EQ(LIBC_NAMESPACE::symlink(target, occupied), 0);
    struct stat before, after, entry_before, entry_after, created;
    ASSERT_EQ(LIBC_NAMESPACE::fstat(fd, &before), 0);
    ASSERT_EQ(LIBC_NAMESPACE::lstat(occupied, &entry_before), 0);
    calls = random_calls = random_error = open_error = collisions = 0;
    random_byte = -1;
    expected_name = nullptr;
    real_files = true;
    int new_fd = symbolic ? LIBC_NAMESPACE::mkostemp(pattern, O_CLOEXEC)
                          : LIBC_NAMESPACE::mkstemp(pattern);
    real_files = false;
    ASSERT_GE(new_fd, 0);
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(random_calls, 12);
    ASSERT_EQ(LIBC_NAMESPACE::fstat(fd, &after), 0);
    ASSERT_EQ(LIBC_NAMESPACE::fstat(new_fd, &created), 0);
    ASSERT_EQ(LIBC_NAMESPACE::lstat(occupied, &entry_after), 0);
    EXPECT_EQ(before.st_dev, after.st_dev);
    EXPECT_EQ(before.st_ino, after.st_ino);
    EXPECT_EQ(before.st_size, after.st_size);
    EXPECT_EQ(before.st_mode, after.st_mode);
    EXPECT_EQ(entry_before.st_ino, entry_after.st_ino);
    EXPECT_EQ(entry_before.st_mode, entry_after.st_mode);
    EXPECT_NE(created.st_ino, before.st_ino);
    char bytes[10] = {};
    EXPECT_EQ(LIBC_NAMESPACE::pread(fd, bytes, 9, 0), ssize_t(9));
    EXPECT_STREQ(bytes, "preserved");
    EXPECT_EQ(LIBC_NAMESPACE::close(fd), 0);
    EXPECT_EQ(LIBC_NAMESPACE::close(new_fd), 0);
    EXPECT_EQ(LIBC_NAMESPACE::unlink(occupied), 0);
    EXPECT_EQ(LIBC_NAMESPACE::unlink(pattern), 0);
    if (symbolic)
      EXPECT_EQ(LIBC_NAMESPACE::unlink(target), 0);
  }
}
