//===-- select timeout syscall injection tests ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/types/fd_set.h"
#include "hdr/types/sigset_t.h"
#include "hdr/types/struct_timespec.h"
#include "hdr/types/struct_timeval.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

namespace {
int result, calls, seen_nfds;
fd_set *seen_read_set, *seen_write_set, *seen_error_set;
bool seen_null_timeout;
timespec seen_timeout;

using Seconds = decltype(timeval::tv_sec);
using Microseconds = decltype(timeval::tv_usec);
constexpr Seconds TIME_MAX =
    LIBC_NAMESPACE::cpp::numeric_limits<Seconds>::max();
constexpr Seconds TIME_MIN =
    LIBC_NAMESPACE::cpp::numeric_limits<Seconds>::min();
constexpr Microseconds USEC_MAX =
    LIBC_NAMESPACE::cpp::numeric_limits<Microseconds>::max();
constexpr Microseconds USEC_MIN =
    LIBC_NAMESPACE::cpp::numeric_limits<Microseconds>::min();
} // namespace

namespace LIBC_NAMESPACE_DECL {
template <typename T, typename SigsetArg>
T select_timeout_test_syscall(long number, int nfds, fd_set *read_set,
                              fd_set *write_set, fd_set *error_set,
                              timespec *timeout, SigsetArg *sigset_arg) {
#if defined(SYS_pselect6_time64)
  EXPECT_EQ(number, long(SYS_pselect6_time64));
#else
  EXPECT_EQ(number, long(SYS_pselect6));
#endif
  ++calls;
  seen_nfds = nfds;
  seen_read_set = read_set;
  seen_write_set = write_set;
  seen_error_set = error_set;
  seen_null_timeout = timeout == nullptr;
  if (timeout != nullptr) {
    seen_timeout = *timeout;
    // Model kernel writeback on both success and failure without waiting.
    *timeout = {0, 42};
  }
  EXPECT_NE(sigset_arg, nullptr);
  if (sigset_arg != nullptr) {
    EXPECT_EQ(sigset_arg->ss, nullptr);
    EXPECT_EQ(sigset_arg->ss_len, sizeof(sigset_t));
  }
  return static_cast<T>(result);
}
} // namespace LIBC_NAMESPACE_DECL

// Exercise the actual generic provider without interposing on runner syscalls.
#define syscall_impl select_timeout_test_syscall
#define select injected_select
#include "src/sys/select/linux/select.cpp"
#undef select
#undef syscall_impl

class LlvmLibcSelectTimeoutTest : public LIBC_NAMESPACE::testing::Test {
public:
  void SetUp() override {
    result = 0;
    calls = 0;
    seen_nfds = 0;
    seen_read_set = seen_write_set = seen_error_set = nullptr;
    seen_null_timeout = false;
    seen_timeout = {};
    LIBC_NAMESPACE::libc_errno = EDOM;
  }

  void TearDown() override { LIBC_NAMESPACE::libc_errno = 0; }

  void expect_timeout(timeval input, timespec expected) {
    const timeval original = input;
    calls = 0;
    EXPECT_EQ(
        LIBC_NAMESPACE::injected_select(0, nullptr, nullptr, nullptr, &input),
        0);
    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(seen_null_timeout);
    EXPECT_EQ(seen_timeout.tv_sec, expected.tv_sec);
    EXPECT_EQ(seen_timeout.tv_nsec, expected.tv_nsec);
    EXPECT_EQ(input.tv_sec, original.tv_sec);
    EXPECT_EQ(input.tv_usec, original.tv_usec);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  }
};

TEST_F(LlvmLibcSelectTimeoutTest, NullTimeoutIsInfiniteAndForwardsArguments) {
  fd_set read_set{}, write_set{}, error_set{};
  result = 3;
  EXPECT_EQ(LIBC_NAMESPACE::injected_select(19, &read_set, &write_set,
                                            &error_set, nullptr),
            3);
  EXPECT_EQ(calls, 1);
  EXPECT_TRUE(seen_null_timeout);
  EXPECT_EQ(seen_nfds, 19);
  EXPECT_EQ(seen_read_set, &read_set);
  EXPECT_EQ(seen_write_set, &write_set);
  EXPECT_EQ(seen_error_set, &error_set);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
}

TEST_F(LlvmLibcSelectTimeoutTest,
       NormalizesExactTimespecWithoutCallerWriteback) {
  expect_timeout({0, 0}, {0, 0});
  expect_timeout({0, 1}, {0, 1000});
  expect_timeout({7, 999999}, {7, 999999000});
  expect_timeout({7, 1000000}, {8, 0});
  expect_timeout({7, 1234567}, {8, 234567000});
  expect_timeout({7, 2000001}, {9, 1000});
}

TEST_F(LlvmLibcSelectTimeoutTest, LargestMicrosecondsDoNotOverflowScaling) {
  expect_timeout({0, USEC_MAX},
                 {USEC_MAX / 1000000, (USEC_MAX % 1000000) * 1000});
}

TEST_F(LlvmLibcSelectTimeoutTest, ExactSecondsLimitDoesNotSaturate) {
  expect_timeout({TIME_MAX, 0}, {TIME_MAX, 0});
  expect_timeout({TIME_MAX, 999999}, {TIME_MAX, 999999000});
  expect_timeout({TIME_MAX - 1, 1000000}, {TIME_MAX, 0});
  expect_timeout({TIME_MAX - 1, 1999999}, {TIME_MAX, 999999000});
}

TEST_F(LlvmLibcSelectTimeoutTest, SecondsOverflowSaturatesBothComponents) {
  expect_timeout({TIME_MAX, 1000000}, {TIME_MAX, 999999999});
  expect_timeout({TIME_MAX - 1, 2000001}, {TIME_MAX, 999999999});
  expect_timeout({TIME_MAX, USEC_MAX}, {TIME_MAX, 999999999});
}

TEST_F(LlvmLibcSelectTimeoutTest, RejectsNegativeComponentsBeforeArithmetic) {
  const timeval invalid[] = {{-1, 0},
                             {-1, 1000000},
                             {0, -1},
                             {1, -1},
                             {TIME_MIN, 0},
                             {TIME_MIN, USEC_MAX},
                             {TIME_MAX, USEC_MIN},
                             {TIME_MIN, USEC_MIN}};
  for (timeval input : invalid) {
    const timeval original = input;
    calls = 0;
    LIBC_NAMESPACE::libc_errno = EDOM;
    EXPECT_EQ(
        LIBC_NAMESPACE::injected_select(0, nullptr, nullptr, nullptr, &input),
        -1);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EINVAL);
    EXPECT_EQ(input.tv_sec, original.tv_sec);
    EXPECT_EQ(input.tv_usec, original.tv_usec);
  }
}

TEST_F(LlvmLibcSelectTimeoutTest,
       SyscallErrorsDoNotRetryOrChangeCallerTimeout) {
  const int errors[] = {EINTR, EBADF, EINVAL, EFAULT, ENOSYS};
  for (int error : errors) {
    timeval input{7, 1234567};
    timeval *timeouts[] = {nullptr, &input};
    for (timeval *timeout : timeouts) {
      result = -error;
      calls = 0;
      LIBC_NAMESPACE::libc_errno = EDOM;
      EXPECT_EQ(LIBC_NAMESPACE::injected_select(-1, nullptr, nullptr, nullptr,
                                                timeout),
                -1);
      EXPECT_EQ(calls, 1);
      EXPECT_EQ(seen_nfds, -1);
      EXPECT_EQ(seen_read_set, nullptr);
      EXPECT_EQ(seen_write_set, nullptr);
      EXPECT_EQ(seen_error_set, nullptr);
      EXPECT_EQ(seen_null_timeout, timeout == nullptr);
      if (timeout != nullptr) {
        EXPECT_EQ(seen_timeout.tv_sec, Seconds(8));
        EXPECT_EQ(seen_timeout.tv_nsec, decltype(timespec::tv_nsec)(234567000));
      }
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
      EXPECT_EQ(input.tv_sec, Seconds(7));
      EXPECT_EQ(input.tv_usec, Microseconds(1234567));
    }
  }
}
