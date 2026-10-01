//===-- Tests for once publication and retry ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/syscall.h"
#include "src/__support/threads/linux/futex_word.h"
#include <linux/futex.h>

namespace LIBC_NAMESPACE_DECL {
static void (*wait_hook)();
static unsigned wakes;
static int operation(...) { return -1; }
template <typename Address, typename... Args>
int operation(Address, uint32_t op, Args...) {
  return op & FUTEX_CMD_MASK;
}

// Inject wakeups at the syscall boundary, without host scheduling assumptions.
template <typename Ret, typename... Args>
Ret observed_syscall(long number, Args... args) {
  if (number == FUTEX_SYSCALL_ID) {
    if (operation(args...) == FUTEX_WAIT_BITSET && wait_hook) {
      wait_hook();
      return 0;
    }
    if (operation(args...) == FUTEX_WAKE)
      ++wakes;
  }
  return syscall_impl<Ret>(number, args...);
}
} // namespace LIBC_NAMESPACE_DECL

#define syscall_impl observed_syscall
#include "src/__support/threads/callonce.h"
#undef syscall_impl
#include "test/IntegrationTest/test.h"

using namespace LIBC_NAMESPACE;
using namespace LIBC_NAMESPACE::callonce_impl;
static CallOnceFlag flag{0};
static unsigned waits;
static void spurious_then_complete() {
  if (++waits == 2)
    flag.store(FINISH, cpp::MemoryOrder::RELEASE);
}
static void reset_owner() {
  ++waits;
  abandon(&flag);
}

TEST_MAIN() {
  flag.store(WAITING);
  wait_hook = spurious_then_complete;
  unsigned calls = 0;
  ASSERT_EQ(callonce(&flag, [&]() { ++calls; }), 0);
  ASSERT_EQ(waits, 2U);
  ASSERT_EQ(calls, 0U);
  ASSERT_TRUE(callonce_fastpath(&flag));

  flag.store(WAITING);
  wait_hook = reset_owner;
  waits = 0;
  ASSERT_EQ(callonce(&flag, [&]() { ++calls; }), 0);
  ASSERT_EQ(waits, 1U);
  ASSERT_EQ(calls, 1U);
  ASSERT_EQ(wakes, 1U);
  ASSERT_TRUE(callonce_fastpath(&flag));

  flag.store(START);
  {
    CompletionGuard interrupted(&flag);
  }
  ASSERT_EQ(flag.load(), NOT_CALLED);
  flag.store(WAITING);
  {
    CompletionGuard interrupted(&flag);
  }
  ASSERT_EQ(flag.load(), NOT_CALLED);
  ASSERT_EQ(wakes, 2U);
  {
    CompletionGuard completed(&flag);
    completed.complete();
  }
  ASSERT_TRUE(callonce_fastpath(&flag));
  ASSERT_EQ(callonce(&flag, [&]() { ++calls; }), 0);
  ASSERT_EQ(calls, 1U);
  flag.store(NOT_CALLED);
  ASSERT_EQ(callonce(&flag, [&]() { flag.store(WAITING); }), 0);
  ASSERT_EQ(wakes, 3U);
  ASSERT_TRUE(callonce_fastpath(&flag));
  return 0;
}
