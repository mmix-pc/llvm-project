//===-- waitid syscall injection tests ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "hdr/sys_wait_macros.h"
#include "hdr/types/id_t.h"
#include "hdr/types/idtype_t.h"
#include "hdr/types/siginfo_t.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

namespace {
struct {
  int result, options;
  unsigned calls;
  idtype_t idtype;
  id_t id;
  siginfo_t *info;
  const siginfo_t *output;
} state;

void reset(idtype_t idtype, id_t id, siginfo_t *info, int options,
           int result = 0, int saved_errno = EDOM) {
  state = {};
  state.idtype = idtype;
  state.id = id;
  state.info = info;
  state.options = options;
  state.result = result;
  LIBC_NAMESPACE::libc_errno = saved_errno;
  if (info) {
    auto *bytes = reinterpret_cast<unsigned char *>(info);
    for (unsigned i = 0; i < sizeof(*info); ++i)
      bytes[i] = 0xa5;
  }
}

void expectUnchangedInfo(const siginfo_t &info) {
  const auto *bytes = reinterpret_cast<const unsigned char *>(&info);
  for (unsigned i = 0; i < sizeof(info); ++i)
    EXPECT_EQ(bytes[i], static_cast<unsigned char>(0xa5));
}

void expectOutput(const siginfo_t &info, const siginfo_t &output) {
  const auto *actual = reinterpret_cast<const unsigned char *>(&info);
  const auto *expected = reinterpret_cast<const unsigned char *>(&output);
  for (unsigned i = 0; i < sizeof(info); ++i)
    EXPECT_EQ(actual[i], expected[i]);
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
template <typename T>
T waitid_test_syscall(long number, idtype_t idtype, id_t id, siginfo_t *info,
                      int options, void *usage) {
  EXPECT_EQ(number, long(SYS_waitid));
  EXPECT_EQ(int(idtype), int(state.idtype));
  EXPECT_EQ(id, state.id);
  EXPECT_EQ(info, state.info);
  EXPECT_EQ(options, state.options);
  EXPECT_EQ(usage, nullptr);
  EXPECT_EQ(++state.calls, 1U);
  // Bound an erroneous EINTR retry loop without issuing any host syscall.
  if (state.calls > 1)
    return static_cast<T>(-EIO);
  if (info && info == state.info) {
    expectUnchangedInfo(*info);
    if (state.output) {
      auto *bytes = reinterpret_cast<unsigned char *>(info);
      const auto *output =
          reinterpret_cast<const unsigned char *>(state.output);
      for (unsigned i = 0; i < sizeof(*info); ++i)
        bytes[i] = output[i];
    }
  }
  return static_cast<T>(state.result);
}
} // namespace LIBC_NAMESPACE_DECL

// Exercise the production provider even when its public export is unselected.
#define syscall_impl waitid_test_syscall
#define waitid injected_waitid
#include "src/sys/wait/linux/waitid.cpp"
#undef waitid
#undef syscall_impl

TEST(LlvmLibcWaitidSyscallTest, ForwardsArgumentsAndPreservesErrnoAndInfo) {
  const idtype_t selectors[] = {P_ALL, P_PID, P_PGID};
  const id_t ids[] = {0, 37, LIBC_NAMESPACE::cpp::numeric_limits<id_t>::max()};
  const int options[] = {WEXITED,
                         WSTOPPED,
                         WCONTINUED,
                         WEXITED | WSTOPPED | WCONTINUED,
                         WEXITED | WNOHANG,
                         WEXITED | WNOWAIT,
                         WEXITED | WSTOPPED | WCONTINUED | WNOHANG | WNOWAIT};
  const int saved_errnos[] = {0, EDOM};
  siginfo_t info;
  for (idtype_t selector : selectors) {
    for (id_t id : ids) {
      for (int flags : options) {
        for (int saved_errno : saved_errnos) {
          reset(selector, id, &info, flags, 0, saved_errno);
          EXPECT_EQ(LIBC_NAMESPACE::injected_waitid(selector, id, &info, flags),
                    0);
          EXPECT_EQ(state.calls, 1U);
          EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), saved_errno);
          expectUnchangedInfo(info);
        }
      }
    }
  }
}

TEST(LlvmLibcWaitidSyscallTest, PropagatesErrorsWithoutRetryOrFallback) {
  const int errors[] = {ECHILD, EINTR, EINVAL, EFAULT, ENOSYS, EAGAIN, EIO};
  siginfo_t info;
  for (int error : errors) {
    reset(P_PID, 37, &info, WEXITED, -error);
    EXPECT_EQ(LIBC_NAMESPACE::injected_waitid(P_PID, 37, &info, WEXITED), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
    EXPECT_EQ(state.calls, 1U);
    expectUnchangedInfo(info);
  }
}

TEST(LlvmLibcWaitidSyscallTest, LeavesOptionValidationToKernel) {
  const int options[] = {0, WNOHANG, WNOWAIT, -1, 0x00800000};
  siginfo_t info;
  for (int flags : options) {
    reset(P_PID, 0, &info, flags, -EINVAL);
    EXPECT_EQ(LIBC_NAMESPACE::injected_waitid(P_PID, 0, &info, flags), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EINVAL);
    EXPECT_EQ(state.calls, 1U);
    expectUnchangedInfo(info);
  }
}

TEST(LlvmLibcWaitidSyscallTest, LeavesNullInfoValidationToKernel) {
  const int results[] = {0, -EFAULT};
  for (int result : results) {
    reset(P_ALL, 0, nullptr, WEXITED, result);
    EXPECT_EQ(LIBC_NAMESPACE::injected_waitid(P_ALL, 0, nullptr, WEXITED),
              result < 0 ? -1 : 0);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), result < 0 ? EFAULT : EDOM);
    EXPECT_EQ(state.calls, 1U);
  }
}

TEST(LlvmLibcWaitidSyscallTest, PreservesKernelExitInfo) {
  // Linux supplies only the low eight exit bits. This is an injected transport
  // check, not evidence of full-width status recovery or real child execution.
  const int statuses[] = {0, 1, 127, 128, 255};
  for (int status : statuses) {
    siginfo_t output{};
    output.si_signo = SIGCHLD;
    output.si_code = CLD_EXITED;
    output.si_pid = 37;
    output.si_uid = 42;
    output.si_status = status;
    siginfo_t info;
    reset(P_PID, 37, &info, WEXITED | WNOWAIT);
    state.output = &output;
    EXPECT_EQ(
        LIBC_NAMESPACE::injected_waitid(P_PID, 37, &info, WEXITED | WNOWAIT),
        0);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
    EXPECT_EQ(state.calls, 1U);
    expectOutput(info, output);
  }
}

TEST(LlvmLibcWaitidSyscallTest, PreservesKernelNoEventInfo) {
  siginfo_t output{};
  siginfo_t info;
  reset(P_ALL, 0, &info, WEXITED | WNOHANG);
  state.output = &output;
  EXPECT_EQ(LIBC_NAMESPACE::injected_waitid(P_ALL, 0, &info, WEXITED | WNOHANG),
            0);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  EXPECT_EQ(state.calls, 1U);
  expectOutput(info, output);
}
