//===-- Priority provider syscall injection tests -------------------------===//
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
  int result, error, which, priority;
  id_t who;
  unsigned gets, sets;
} state;

void reset(int saved_errno = EDOM) {
  state = {};
  LIBC_NAMESPACE::libc_errno = saved_errno;
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
namespace priority_test_syscalls {
ErrorOr<int> getpriority(int which, id_t who) {
  ++state.gets;
  state.which = which;
  state.who = who;
  if (state.error)
    return Error(state.error);
  return state.result;
}

ErrorOr<int> setpriority(int which, id_t who, int priority) {
  ++state.sets;
  state.which = which;
  state.who = who;
  state.priority = priority;
  if (state.error)
    return Error(state.error);
  return 0;
}
} // namespace priority_test_syscalls
} // namespace LIBC_NAMESPACE_DECL

// Include the real providers without linking their entrypoint objects. These
// tests observe forwarding/error translation, not kernel task/UID selection.
#define linux_syscalls priority_test_syscalls
#include "src/sys/resource/linux/getpriority.cpp"
#include "src/sys/resource/linux/setpriority.cpp"
#undef linux_syscalls

TEST(LlvmLibcPrioritySyscallTest, DecodesEveryNiceValueWithoutChangingErrno) {
  const int saved_errnos[] = {0, EDOM};
  for (int saved_errno : saved_errnos) {
    for (int nice = -20; nice <= 19; ++nice) {
      reset(saved_errno);
      state.result = 20 - nice;
      EXPECT_EQ(LIBC_NAMESPACE::getpriority(PRIO_PROCESS, 0), nice);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), saved_errno);
      EXPECT_EQ(state.gets, 1U);
      EXPECT_EQ(state.sets, 0U);
    }
  }
}

TEST(LlvmLibcPrioritySyscallTest, ForwardsSelectorsAndFullWidthIdsUnchanged) {
  const int selectors[] = {PRIO_PROCESS, PRIO_PGRP, PRIO_USER, -1, 99};
  const id_t ids[] = {0, 1, 1234,
                      LIBC_NAMESPACE::cpp::numeric_limits<id_t>::max()};
  for (int which : selectors) {
    for (id_t who : ids) {
      reset();
      state.result = 20;
      EXPECT_EQ(LIBC_NAMESPACE::getpriority(which, who), 0);
      EXPECT_EQ(state.which, which);
      EXPECT_EQ(state.who, who);
      EXPECT_EQ(state.gets, 1U);
      EXPECT_EQ(state.sets, 0U);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);

      reset();
      EXPECT_EQ(LIBC_NAMESPACE::setpriority(which, who, -1), 0);
      EXPECT_EQ(state.which, which);
      EXPECT_EQ(state.who, who);
      EXPECT_EQ(state.priority, -1);
      EXPECT_EQ(state.gets, 0U);
      EXPECT_EQ(state.sets, 1U);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
    }
  }
}

TEST(LlvmLibcPrioritySyscallTest, LeavesAbsolutePriorityClampingToKernel) {
  const int priorities[] = {
      LIBC_NAMESPACE::cpp::numeric_limits<int>::min(), -21, -20, -1, 0, 19, 20,
      LIBC_NAMESPACE::cpp::numeric_limits<int>::max()};
  const int saved_errnos[] = {0, EDOM};
  for (int saved_errno : saved_errnos) {
    for (int priority : priorities) {
      reset(saved_errno);
      EXPECT_EQ(LIBC_NAMESPACE::setpriority(PRIO_PROCESS, 0, priority), 0);
      EXPECT_EQ(state.priority, priority);
      EXPECT_EQ(state.gets, 0U);
      EXPECT_EQ(state.sets, 1U);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), saved_errno);
    }
  }
}

TEST(LlvmLibcPrioritySyscallTest, GetErrorsReplaceErrnoWithoutRetry) {
  const int errors[] = {EINVAL, ESRCH, EPERM, EACCES, EINTR, ENOSYS};
  for (int error : errors) {
    reset();
    state.error = error;
    EXPECT_EQ(LIBC_NAMESPACE::getpriority(PRIO_USER, 1234), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
    EXPECT_EQ(state.which, PRIO_USER);
    EXPECT_EQ(state.who, id_t(1234));
    EXPECT_EQ(state.gets, 1U);
    EXPECT_EQ(state.sets, 0U);
  }
}

TEST(LlvmLibcPrioritySyscallTest, SetErrorsAreNotRemappedRetriedOrRolledBack) {
  const int errors[] = {EINVAL, ESRCH, EPERM, EACCES, EINTR, ENOSYS};
  const int selectors[] = {PRIO_PROCESS, PRIO_PGRP, PRIO_USER};
  for (int which : selectors) {
    for (int error : errors) {
      reset();
      state.error = error;
      EXPECT_EQ(LIBC_NAMESPACE::setpriority(which, 1234, -20), -1);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
      EXPECT_EQ(state.which, which);
      EXPECT_EQ(state.who, id_t(1234));
      EXPECT_EQ(state.priority, -20);
      EXPECT_EQ(state.gets, 0U);
      EXPECT_EQ(state.sets, 1U);
    }
  }
}
