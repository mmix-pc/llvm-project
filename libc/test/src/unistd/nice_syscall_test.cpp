//===-- Nice provider syscall injection tests -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/sys_resource_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/getpriority.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/setpriority.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"

namespace {
struct {
  int initial, observed, requested, get_error[2], set_error;
  unsigned gets, sets;
} state;

void reset(int initial = 0, int saved_errno = EDOM) {
  state = {};
  state.initial = initial;
  state.observed = initial;
  LIBC_NAMESPACE::libc_errno = saved_errno;
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
namespace nice_test_syscalls {
ErrorOr<int> getpriority(int which, id_t who) {
  EXPECT_EQ(which, PRIO_PROCESS);
  EXPECT_EQ(who, id_t(0));
  unsigned call = state.gets++;
  EXPECT_LT(call, 2U);
  if (call >= 2)
    return Error(EIO);
  EXPECT_EQ(state.sets, call);
  if (state.get_error[call])
    return Error(state.get_error[call]);
  return 20 - (call == 0 ? state.initial : state.observed);
}

ErrorOr<int> setpriority(int which, id_t who, int priority) {
  EXPECT_EQ(which, PRIO_PROCESS);
  EXPECT_EQ(who, id_t(0));
  EXPECT_EQ(state.gets, 1U);
  EXPECT_EQ(++state.sets, 1U);
  state.requested = priority;
  if (state.set_error)
    return Error(state.set_error);
  return 0;
}
} // namespace nice_test_syscalls
} // namespace LIBC_NAMESPACE_DECL

// Exercise production control flow without changing any host task's priority.
#define linux_syscalls nice_test_syscalls
#define nice injected_nice
#include "src/unistd/linux/nice.cpp"
#undef nice
#undef linux_syscalls

TEST(LlvmLibcNiceSyscallTest, AllNiceValuesPreserveErrno) {
  const int saved_errnos[] = {0, EDOM};
  for (int saved_errno : saved_errnos) {
    for (int initial = -20; initial <= 19; ++initial) {
      reset(initial, saved_errno);
      EXPECT_EQ(LIBC_NAMESPACE::injected_nice(0), initial);
      EXPECT_EQ(state.requested, initial);
      EXPECT_EQ(state.gets, 2U);
      EXPECT_EQ(state.sets, 1U);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), saved_errno);
    }
  }
}

TEST(LlvmLibcNiceSyscallTest, ClampsBeforeAdding) {
  const int extremes[] = {LIBC_NAMESPACE::cpp::numeric_limits<int>::min(),
                          LIBC_NAMESPACE::cpp::numeric_limits<int>::max()};
  for (int initial = -20; initial <= 19; ++initial) {
    // Exhaust the ordinary increments, including both saturation boundaries.
    for (int increment = -40; increment <= 40; ++increment) {
      reset(initial);
      int expected = initial + increment;
      if (expected < -20)
        expected = -20;
      if (expected > 19)
        expected = 19;
      state.observed = expected;
      EXPECT_EQ(LIBC_NAMESPACE::injected_nice(increment), expected);
      EXPECT_EQ(state.requested, expected);
      EXPECT_EQ(state.gets, 2U);
      EXPECT_EQ(state.sets, 1U);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
    }
    for (int increment : extremes) {
      reset(initial);
      const int expected = increment < 0 ? -20 : 19;
      state.observed = expected;
      EXPECT_EQ(LIBC_NAMESPACE::injected_nice(increment), expected);
      EXPECT_EQ(state.requested, expected);
      EXPECT_EQ(state.gets, 2U);
      EXPECT_EQ(state.sets, 1U);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
    }
  }
}

TEST(LlvmLibcNiceSyscallTest, ReturnsObservedValueAfterUpdate) {
  const int saved_errnos[] = {0, EDOM};
  for (int saved_errno : saved_errnos) {
    reset(-5, saved_errno);
    // Another actor can change the value between the set and the readback.
    state.observed = -1;
    EXPECT_EQ(LIBC_NAMESPACE::injected_nice(2), -1);
    EXPECT_EQ(state.requested, -3);
    EXPECT_EQ(state.gets, 2U);
    EXPECT_EQ(state.sets, 1U);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), saved_errno);
  }
}

TEST(LlvmLibcNiceSyscallTest, InitialQueryFailureDoesNotSetPriority) {
  const int errors[] = {ESRCH, EACCES, EPERM, EINVAL, EINTR, ENOSYS};
  for (int error : errors) {
    reset();
    state.get_error[0] = error;
    EXPECT_EQ(LIBC_NAMESPACE::injected_nice(1), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
    EXPECT_EQ(state.gets, 1U);
    EXPECT_EQ(state.sets, 0U);
  }
}

TEST(LlvmLibcNiceSyscallTest, SetFailureMapsPermissionErrorWithoutReadback) {
  const int errors[] = {EACCES, EPERM, ESRCH, EINVAL, EINTR, ENOSYS};
  for (int error : errors) {
    reset(-1);
    state.set_error = error;
    EXPECT_EQ(LIBC_NAMESPACE::injected_nice(-1), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error == EACCES ? EPERM : error);
    EXPECT_EQ(state.requested, -2);
    EXPECT_EQ(state.gets, 1U);
    EXPECT_EQ(state.sets, 1U);
  }
}

TEST(LlvmLibcNiceSyscallTest, ReadbackFailureDoesNotRetryOrRollBack) {
  const int errors[] = {ESRCH, EACCES, EPERM, EINVAL, EINTR, ENOSYS};
  for (int error : errors) {
    reset();
    state.get_error[1] = error;
    EXPECT_EQ(LIBC_NAMESPACE::injected_nice(1), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
    EXPECT_EQ(state.requested, 1);
    EXPECT_EQ(state.gets, 2U);
    EXPECT_EQ(state.sets, 1U);
  }
}
