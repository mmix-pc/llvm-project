//===-- Resource failures through the real nftw engine ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/exit.h"
#include "src/__support/OSUtil/linux/stat/stat_via_statx.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/chdir.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/fchdir.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/ftw/walk.h"
#include "test/UnitTest/Test.h"
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

namespace {
bool tracking, fail_read, fail_close, invalid_owner, missing_cloexec;
unsigned reads, opens, closes, live_fds, peak_fds, callbacks;
int owned_fd;
void *allocations[1024];
size_t live_allocations, first_batch_allocations, fault_allocations;
bool stop_callback;

void remember(void *pointer) {
  if (!pointer)
    return;
  if (live_allocations == 1024)
    __builtin_trap();
  allocations[live_allocations++] = pointer;
}
void forget(void *pointer) {
  if (!pointer)
    return;
  for (size_t i = 0; i < live_allocations; ++i) {
    if (allocations[i] == pointer) {
      allocations[i] = allocations[--live_allocations];
      return;
    }
  }
  invalid_owner = true;
}
} // namespace

extern "C" void *__real_malloc(size_t);
extern "C" void *__real_realloc(void *, size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t size) {
  void *pointer = __real_malloc(size);
  if (tracking)
    remember(pointer);
  return pointer;
}
extern "C" void *__wrap_realloc(void *old, size_t size) {
  void *pointer = __real_realloc(old, size);
  if (tracking && pointer) {
    forget(old);
    remember(pointer);
  }
  return pointer;
}
extern "C" void __wrap_free(void *pointer) {
  if (tracking)
    forget(pointer);
  __real_free(pointer);
}

namespace LIBC_NAMESPACE_DECL {
namespace resource_syscalls {
using linux_syscalls::chdir;
using linux_syscalls::fchdir;
ErrorOr<int> open(const char *path, int flags, mode_t mode) {
  auto result = linux_syscalls::open(path, flags, mode);
  if (tracking && result) {
    ++opens;
    if (++live_fds > peak_fds)
      peak_fds = live_fds;
    if (live_fds != 1)
      invalid_owner = true;
    owned_fd = *result;
    missing_cloexec |= !(flags & O_CLOEXEC);
  }
  return result;
}
ErrorOr<int> close(int fd) {
  if (tracking) {
    ++closes;
    if (live_fds != 1 || fd != owned_fd)
      invalid_owner = true;
    else
      --live_fds;
  }
  auto result = linux_syscalls::close(fd);
  return tracking && fail_close && result ? ErrorOr<int>(Error(EIO)) : result;
}
} // namespace resource_syscalls

template <class T>
T resource_syscall(long number, int fd, void *buffer, size_t size) {
  if (number != SYS_getdents64)
    __builtin_trap();
  ++reads;
  if (fail_read && reads == 2) {
    fault_allocations = live_allocations;
    return T(-EIO);
  }
  // Force several real batches; the second read follows allocated children.
  auto result = syscall_impl<T>(number, fd, buffer, size_t(128) < size ? 128 : size);
  if (reads == 1)
    first_batch_allocations = live_allocations;
  return result;
}
} // namespace LIBC_NAMESPACE_DECL

// Substitute only I/O transport. Both traversal state machines remain real.
#define linux_syscalls resource_syscalls
#define syscall_impl resource_syscall
#include "src/fts/linux_io.h"
#undef syscall_impl
#undef linux_syscalls
#include "src/fts/engine.cpp"
#define linux_syscalls resource_syscalls
#include "src/ftw/linux/walk.cpp"
#undef linux_syscalls

namespace {
int callback(const char *, const struct stat *, int, struct FTW *) {
  ++callbacks;
  return stop_callback ? 7 : 0;
}

class LlvmLibcNftwResourceTest : public LIBC_NAMESPACE::testing::Test {
protected:
  char root[64];
  void SetUp() override {
    __builtin_memcpy(root, "nftw_resource_XXXXXX", sizeof("nftw_resource_XXXXXX"));
    ASSERT_NE(::mkdtemp(root), static_cast<char *>(nullptr));
    int dir = ::open(root, O_RDONLY | O_DIRECTORY);
    ASSERT_GE(dir, 0);
    for (unsigned i = 0; i < 10; ++i) {
      char name[] = "child0";
      name[5] = char('0' + i);
      int fd = ::openat(dir, name, O_CREAT | O_EXCL | O_RDWR, 0600);
      ASSERT_GE(fd, 0);
      ASSERT_EQ(::close(fd), 0);
    }
    ASSERT_EQ(::close(dir), 0);
    tracking = fail_read = fail_close = invalid_owner = missing_cloexec = false;
    stop_callback = false;
    reads = opens = closes = live_fds = peak_fds = callbacks = 0;
    live_allocations = first_batch_allocations = fault_allocations = 0;
  }
  void TearDown() override {
    tracking = false;
    int dir = ::open(root, O_RDONLY | O_DIRECTORY);
    ASSERT_GE(dir, 0);
    for (unsigned i = 0; i < 10; ++i) {
      char name[] = "child0";
      name[5] = char('0' + i);
      EXPECT_EQ(::unlinkat(dir, name, 0), 0);
    }
    EXPECT_EQ(::close(dir), 0);
    EXPECT_EQ(::rmdir(root), 0);
  }
  int walk() {
    tracking = true;
    int result = LIBC_NAMESPACE::ftw_internal::walk(
        root, {callback, nullptr}, 1, FTW_PHYS | FTW_CHDIR);
    tracking = false;
    return result;
  }
  void retired() {
    EXPECT_FALSE(invalid_owner);
    EXPECT_FALSE(missing_cloexec);
    EXPECT_EQ(peak_fds, 1U);
    EXPECT_EQ(live_fds, 0U);
    EXPECT_EQ(opens, closes);
    EXPECT_EQ(live_allocations, size_t(0));
  }
};
} // namespace

TEST_F(LlvmLibcNftwResourceTest, EnumerationAndCallbackShareOneDescriptor) {
  EXPECT_EQ(walk(), 0);
  EXPECT_EQ(callbacks, 11U);
  EXPECT_GT(reads, 1U);
  retired();
}

TEST_F(LlvmLibcNftwResourceTest, PartialReadFailureRetiresAllocatedChildren) {
  fail_read = true;
  EXPECT_EQ(walk(), -1);
  EXPECT_EQ(int(libc_errno), EIO);
  EXPECT_EQ(reads, 2U);
  EXPECT_GT(fault_allocations, first_batch_allocations);
  EXPECT_EQ(callbacks, 0U);
  retired();
}

TEST_F(LlvmLibcNftwResourceTest, StopAndConsumedCloseErrorDoNotLeakOrRetry) {
  stop_callback = fail_close = true;
  EXPECT_EQ(walk(), 7);
  EXPECT_EQ(callbacks, 1U);
  retired();
}
