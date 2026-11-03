//===-- Process-group signal dispatch tests -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/limits_macros.h"
#include "src/__support/libc_errno.h"
#include "src/signal/kill.h"
#include "src/signal/killpg.h"
#include "test/UnitTest/Test.h"

static pid_t expected;
static int signal_number, error, calls;
namespace LIBC_NAMESPACE_DECL {
int kill(pid_t pid, int sig) {
  ++calls;
  EXPECT_EQ(pid, expected);
  EXPECT_EQ(sig, signal_number);
  if (error) {
    libc_errno = error;
    return -1;
  }
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL

TEST(LlvmLibcKillpgTest, RejectNegativeGroupsWithoutNegationOrDelivery) {
  const pid_t groups[] = {INT_MIN, -2, -1};
  for (pid_t group : groups) {
    calls = 0;
    libc_errno = 0;
    EXPECT_EQ(LIBC_NAMESPACE::killpg(group, 0), -1);
    EXPECT_EQ(int(libc_errno), EINVAL);
    EXPECT_EQ(calls, 0);
  }
}

TEST(LlvmLibcKillpgTest, ForwardResultsIncludingLinuxExtensions) {
  // Zero and one are Linux extensions, not portable process-group inputs.
  const pid_t groups[] = {0, 1, 2, 42, INT_MAX};
  const int errors[] = {0, EPERM, ESRCH, EINVAL};
  const int signals[] = {0, 1, 32, 64, 65, -1};
  for (pid_t group : groups)
    for (int sig : signals)
      for (int e : errors) {
        expected = -group;
        signal_number = sig;
        error = e;
        calls = 0;
        libc_errno = EDOM;
        EXPECT_EQ(LIBC_NAMESPACE::killpg(group, sig), error ? -1 : 0);
        EXPECT_EQ(int(libc_errno), error ? error : EDOM);
        EXPECT_EQ(calls, 1);
      }
}
