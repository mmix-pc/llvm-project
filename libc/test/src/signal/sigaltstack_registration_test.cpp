//===-- Tests for Linux alternate stack registration -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/signal_macros.h"
#include "src/__support/libc_errno.h"
#include "src/signal/sigaltstack.h"
#include "test/UnitTest/ErrnoCheckingTest.h"

// Linux supports SS_AUTODISARM even when the public headers do not expose it.
static constexpr unsigned AUTODISARM = 1U << 31;
alignas(16) static char alternate_stack[64 * 1024];

class LlvmLibcSigaltstackRegistrationTest
    : public LIBC_NAMESPACE::testing::ErrnoCheckingTest {
  stack_t original{};

public:
  void SetUp() override {
    LIBC_NAMESPACE::testing::ErrnoCheckingTest::SetUp();
    ASSERT_EQ(LIBC_NAMESPACE::sigaltstack(nullptr, &original), 0);
  }
  void TearDown() override {
    EXPECT_EQ(LIBC_NAMESPACE::sigaltstack(&original, nullptr), 0);
    LIBC_NAMESPACE::testing::ErrnoCheckingTest::TearDown();
  }
};

TEST_F(LlvmLibcSigaltstackRegistrationTest, RegisterQueryAndDisable) {
  stack_t stack{};
  stack.ss_sp = alternate_stack;
  stack.ss_size = sizeof(alternate_stack);
  stack_t previous{}, queried{};
  ASSERT_EQ(LIBC_NAMESPACE::sigaltstack(&stack, &previous), 0);
  ASSERT_EQ(LIBC_NAMESPACE::sigaltstack(nullptr, &queried), 0);
  EXPECT_EQ(queried.ss_sp, stack.ss_sp);
  EXPECT_EQ(queried.ss_size, stack.ss_size);
  EXPECT_EQ(queried.ss_flags, 0);
  // Linux accepts SS_ONSTACK as an input mode; it derives the queried status
  // from the current stack pointer rather than storing that status bit.
  stack.ss_flags = SS_ONSTACK;
  ASSERT_EQ(LIBC_NAMESPACE::sigaltstack(&stack, nullptr), 0);
  ASSERT_EQ(LIBC_NAMESPACE::sigaltstack(nullptr, &queried), 0);
  EXPECT_EQ(queried.ss_flags, 0);
  EXPECT_EQ(LIBC_NAMESPACE::sigaltstack(nullptr, nullptr), 0);

  stack = {};
  stack.ss_flags = SS_DISABLE;
  LIBC_NAMESPACE::libc_errno = 0;
  ASSERT_EQ(LIBC_NAMESPACE::sigaltstack(&stack, &previous), 0);
  EXPECT_EQ(static_cast<int>(LIBC_NAMESPACE::libc_errno), 0);
  EXPECT_EQ(previous.ss_sp, static_cast<void *>(alternate_stack));
  ASSERT_EQ(LIBC_NAMESPACE::sigaltstack(nullptr, &queried), 0);
  EXPECT_EQ(queried.ss_flags, static_cast<int>(SS_DISABLE));
}

TEST_F(LlvmLibcSigaltstackRegistrationTest, Autodisarm) {
  stack_t stack{};
  stack.ss_sp = alternate_stack;
  stack.ss_size = sizeof(alternate_stack);
  stack.ss_flags = static_cast<int>(AUTODISARM);
  ASSERT_EQ(LIBC_NAMESPACE::sigaltstack(&stack, nullptr), 0);
  stack_t queried{};
  ASSERT_EQ(LIBC_NAMESPACE::sigaltstack(nullptr, &queried), 0);
  EXPECT_EQ(static_cast<unsigned>(queried.ss_flags), AUTODISARM);
}

TEST_F(LlvmLibcSigaltstackRegistrationTest, KernelErrors) {
  stack_t stack{};
  stack.ss_sp = alternate_stack;
  stack.ss_flags = SS_ONSTACK | SS_DISABLE;
  EXPECT_EQ(LIBC_NAMESPACE::sigaltstack(&stack, nullptr), -1);
  EXPECT_EQ(static_cast<int>(LIBC_NAMESPACE::libc_errno), EINVAL);
  LIBC_NAMESPACE::libc_errno = 0;
  stack.ss_flags = 0;
  EXPECT_EQ(LIBC_NAMESPACE::sigaltstack(&stack, nullptr), -1);
  EXPECT_EQ(static_cast<int>(LIBC_NAMESPACE::libc_errno), ENOMEM);
  LIBC_NAMESPACE::libc_errno = 0;
  EXPECT_EQ(LIBC_NAMESPACE::sigaltstack(reinterpret_cast<stack_t *>(1), nullptr),
            -1);
  EXPECT_EQ(static_cast<int>(LIBC_NAMESPACE::libc_errno), EFAULT);
  LIBC_NAMESPACE::libc_errno = 0;
  EXPECT_EQ(LIBC_NAMESPACE::sigaltstack(nullptr, reinterpret_cast<stack_t *>(1)),
            -1);
  EXPECT_EQ(static_cast<int>(LIBC_NAMESPACE::libc_errno), EFAULT);
  LIBC_NAMESPACE::libc_errno = 0;
}
