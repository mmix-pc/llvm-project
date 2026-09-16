//===-- Tests for signal action initialization ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Exercise the common wrapper with a controlled sigaction boundary, before
// kernel mask sanitization or restorer replacement can hide uninitialized data.
// Compile it here so the test's pattern initialization also applies to action.
#include "src/signal/linux/signal.cpp"
#include "test/UnitTest/Test.h"

static void handler(int) {}
static void previous_handler(int) {}
static int calls;
static void (*expected_handler)(int);

namespace LIBC_NAMESPACE_DECL {
int sigaction(int signo, const struct sigaction *action,
              struct sigaction *old_action) {
  ++calls;
  EXPECT_EQ(signo, SIGUSR1);
  EXPECT_TRUE(action->sa_handler == expected_handler);
  EXPECT_EQ(action->sa_flags, SA_RESTART);
  EXPECT_TRUE(action->sa_restorer == nullptr);
  const auto *bytes = reinterpret_cast<const unsigned char *>(&action->sa_mask);
  for (unsigned i = 0; i < sizeof(action->sa_mask); ++i)
    EXPECT_EQ(bytes[i], static_cast<unsigned char>(0));
  old_action->sa_handler = previous_handler;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL

TEST(LlvmLibcSignalInitializationTest, InitializesCompleteAction) {
  void (*handlers[])(int) = {handler, SIG_DFL, SIG_IGN};
  calls = 0;
  for (auto value : handlers) {
    expected_handler = value;
    EXPECT_TRUE(LIBC_NAMESPACE::signal(SIGUSR1, value) == previous_handler);
  }
  EXPECT_EQ(calls, 3);
}
