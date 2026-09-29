//===-- Tests for thread stack rollback ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/sys_mman_macros.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/mmap.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/munmap.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/threads/thread.h"
#include "test/IntegrationTest/test.h"

#include <linux/filter.h>
#include <linux/param.h>
#include <linux/prctl.h>
#include <linux/seccomp.h>
#include <sys/syscall.h>

using LIBC_NAMESPACE::syscall_impl;
using LIBC_NAMESPACE::Thread;

static constexpr size_t STACK_SIZE = 64 * 1024;

static int runner(void *) { return 42; }

static int unexpected_runner(void *) {
  ASSERT_TRUE(false);
  return 0;
}

static void run_and_join(void *stack, size_t guard) {
  Thread thread;
  ASSERT_EQ(thread.run(runner, nullptr, stack, STACK_SIZE, guard), 0);
  int result = 0;
  ASSERT_EQ(thread.join(&result), 0);
  ASSERT_EQ(result, 42);
}

// Observe virtual size, not RSS or VMA count: untouched mappings consume no
// resident pages and adjacent failed stack mappings can merge into one VMA.
static size_t mapped_pages() {
  long fd =
      syscall_impl<long>(SYS_openat, AT_FDCWD, "/proc/self/statm", O_RDONLY, 0);
  ASSERT_TRUE(fd >= 0);
  char buffer[128];
  long length = syscall_impl<long>(SYS_read, fd, buffer, sizeof(buffer));
  ASSERT_TRUE(length > 0);
  ASSERT_EQ(syscall_impl<long>(SYS_close, fd), 0L);
  size_t pages = 0;
  long i = 0;
  for (; i < length && buffer[i] >= '0' && buffer[i] <= '9'; ++i)
    pages = pages * 10 + size_t(buffer[i] - '0');
  ASSERT_TRUE(i > 0);
  ASSERT_TRUE(i < length);
  ASSERT_EQ(buffer[i], ' ');
  return pages;
}

// These filters are process-local and installed only after normal thread
// creation has passed. Exercise real allocation without replacing libc code.
static void reject_syscall(unsigned number, unsigned error) {
  sock_filter filter[] = {
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, __builtin_offsetof(seccomp_data, nr)),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, number, 0, 1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | error),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
  };
  sock_fprog program = {sizeof(filter) / sizeof(filter[0]), filter};
  ASSERT_EQ(syscall_impl<long>(SYS_prctl, PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0), 0L);
  ASSERT_EQ(syscall_impl<long>(SYS_prctl, PR_SET_SECCOMP, SECCOMP_MODE_FILTER,
                               &program),
            0L);
}

TEST_MAIN() {
  run_and_join(nullptr, 0);
  run_and_join(nullptr, EXEC_PAGESIZE);
  run_and_join(nullptr, 2 * EXEC_PAGESIZE);

  auto mapping = LIBC_NAMESPACE::linux_syscalls::mmap(
      nullptr, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
      -1, 0);
  ASSERT_TRUE(mapping.has_value());
  auto *caller_stack = static_cast<unsigned char *>(mapping.value());
  caller_stack[0] = 123;

  reject_syscall(SYS_mprotect, EPERM);
  const size_t before = mapped_pages();
  for (size_t guard = EXEC_PAGESIZE; guard <= 2 * EXEC_PAGESIZE;
       guard += EXEC_PAGESIZE) {
    Thread thread;
    ASSERT_EQ(
        thread.run(unexpected_runner, nullptr, nullptr, STACK_SIZE, guard),
        EPERM);
    ASSERT_EQ(thread.attrib,
              static_cast<LIBC_NAMESPACE::ThreadAttributes *>(nullptr));
    ASSERT_EQ(mapped_pages(), before);
  }

  // No guard protection is needed for a zero-guard or caller-owned stack.
  run_and_join(nullptr, 0);
  run_and_join(caller_stack, 0);
  ASSERT_EQ(caller_stack[0], 123);
  caller_stack[0] = 45;
  ASSERT_EQ(caller_stack[0], 45);
  ASSERT_TRUE(LIBC_NAMESPACE::linux_syscalls::munmap(caller_stack, STACK_SIZE)
                  .has_value());

#ifdef SYS_mmap2
  reject_syscall(SYS_mmap2, ENOMEM);
#else
  reject_syscall(SYS_mmap, ENOMEM);
#endif
  const size_t before_mmap_failure = mapped_pages();
  Thread thread;
  ASSERT_EQ(thread.run(unexpected_runner, nullptr, nullptr, STACK_SIZE,
                       EXEC_PAGESIZE),
            ENOMEM);
  ASSERT_EQ(thread.attrib,
            static_cast<LIBC_NAMESPACE::ThreadAttributes *>(nullptr));
  ASSERT_EQ(mapped_pages(), before_mmap_failure);
  return 0;
}
