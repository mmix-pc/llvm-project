//===-- MMIX readiness syscall transport tests ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/poll/poll.h"
#include "src/poll/ppoll.h"
#include "src/sys/select/pselect.h"
#include "src/sys/select/select.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include "src/signal/linux/mmix/private_signals.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

namespace {
long reply, seen_number;
unsigned calls;
bool has_timeout, has_mask;
timespec seen_timeout;
uint64_t seen_mask;
unsigned long seen_size;
long seen_args[4];

long observe(long number, long a1, long a2, long a3, long a4, long a5,
             long a6) {
  ++calls;
  seen_number = number;
  seen_args[0] = a1;
  seen_args[1] = a2;
  seen_args[2] = a3;
  seen_args[3] = a4;
  auto *timeout = reinterpret_cast<timespec *>(number == SYS_ppoll ? a3 : a5);
  has_timeout = timeout != nullptr;
  if (timeout) {
    seen_timeout = *timeout;
    *timeout = {0, 0};
  }
  const uint64_t *mask;
  if (number == SYS_ppoll) {
    mask = reinterpret_cast<const uint64_t *>(a4);
    seen_size = a5;
  } else {
    EXPECT_EQ(number, long(SYS_pselect6));
    // Inspect the six-argument Linux ABI as words, without aliasing the
    // provider's anonymous parameter-block type.
    __builtin_memcpy(&mask, reinterpret_cast<void *>(a6), sizeof(mask));
    __builtin_memcpy(&seen_size, reinterpret_cast<char *>(a6) + sizeof(mask),
                     sizeof(seen_size));
  }
  has_mask = mask != nullptr;
  if (mask)
    seen_mask = *mask;
  return reply;
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
template <typename T, typename Mask>
T readiness_syscall(long number, int nfds, fd_set *r, fd_set *w, fd_set *e,
                    timespec *timeout, Mask *mask) {
  return observe(number, nfds, reinterpret_cast<long>(r),
                 reinterpret_cast<long>(w), reinterpret_cast<long>(e),
                 reinterpret_cast<long>(timeout), reinterpret_cast<long>(mask));
}
template <typename T>
T readiness_syscall(long number, pollfd *fds, nfds_t count, timespec *timeout,
                    sigset_t *mask, int size) {
  return observe(number, reinterpret_cast<long>(fds), count,
                 reinterpret_cast<long>(timeout), reinterpret_cast<long>(mask),
                 size, 0);
}
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
namespace mmix {
long cancellation_syscall(unsigned long number, long a1, long a2, long a3,
                          long a4, long a5, long a6) {
  return observe(number, a1, a2, a3, a4, a5, a6);
}
} // namespace mmix
#endif
} // namespace LIBC_NAMESPACE_DECL

// Keep runner operations separate while testing the actual provider bodies.
#define syscall_impl readiness_syscall
#define pselect injected_pselect
#define select injected_select
#define ppoll injected_ppoll
#define poll injected_poll
namespace LIBC_NAMESPACE_DECL {
int injected_pselect(int, fd_set *, fd_set *, fd_set *, const timespec *,
                     const sigset_t *);
int injected_ppoll(pollfd *, nfds_t, const timespec *, const sigset_t *);
} // namespace LIBC_NAMESPACE_DECL
#include "src/sys/select/linux/mmix/pselect.cpp"
#include "src/sys/select/linux/mmix/select.cpp"
#include "src/poll/linux/mmix/ppoll.cpp"
#include "src/poll/linux/mmix/poll.cpp"
#undef poll
#undef ppoll
#undef select
#undef pselect
#undef syscall_impl

TEST(LlvmLibcMmixReadiness, InfiniteAndFiniteTimeouts) {
  reply = 0;
  calls = 0;
  EXPECT_EQ(LIBC_NAMESPACE::injected_poll(nullptr, 0, -1), 0);
  EXPECT_FALSE(has_timeout);
  EXPECT_FALSE(has_mask);
  EXPECT_EQ(seen_size, 8UL);
  EXPECT_EQ(LIBC_NAMESPACE::injected_poll(nullptr, 0, 1234), 0);
  EXPECT_TRUE(has_timeout);
  EXPECT_EQ(seen_timeout.tv_sec, time_t(1));
  EXPECT_EQ(seen_timeout.tv_nsec, 234000000L);
  EXPECT_EQ(LIBC_NAMESPACE::injected_select(0, nullptr, nullptr, nullptr, nullptr), 0);
  EXPECT_FALSE(has_timeout);
  EXPECT_FALSE(has_mask);
  timeval tv{7, 1234567};
  EXPECT_EQ(LIBC_NAMESPACE::injected_select(0, nullptr, nullptr, nullptr, &tv), 0);
  EXPECT_EQ(seen_timeout.tv_sec, time_t(8));
  EXPECT_EQ(seen_timeout.tv_nsec, 234567000L);
  EXPECT_EQ(tv.tv_sec, time_t(7));
  EXPECT_EQ(tv.tv_usec, decltype(tv.tv_usec)(1234567));
  EXPECT_EQ(calls, 4U);
}

TEST(LlvmLibcMmixReadiness, MaskTranslationPreservesInputsAndError) {
  sigset_t mask{};
  for (auto &word : mask.__signals)
    word = ~0UL;
  fd_set r{}, w{}, e{};
  pollfd fds[2]{};
  const long results[] = {0, 2, -EINTR, -EBADF, -EINVAL};
  for (long result : results) {
    reply = result;
    calls = 0;
    timespec timeout{3, 47};
    libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::injected_pselect(17, &r, &w, &e, &timeout, &mask),
              result < 0 ? -1 : int(result));
    EXPECT_EQ(seen_number, long(SYS_pselect6));
    EXPECT_EQ(seen_args[0], 17L);
    EXPECT_EQ(seen_args[1], reinterpret_cast<long>(&r));
    EXPECT_EQ(seen_args[2], reinterpret_cast<long>(&w));
    EXPECT_EQ(seen_args[3], reinterpret_cast<long>(&e));
    EXPECT_EQ(int(libc_errno), result < 0 ? int(-result) : EDOM);
    EXPECT_TRUE(has_mask);
    EXPECT_EQ(seen_size, 8UL);
    EXPECT_EQ(seen_mask, ~LIBC_NAMESPACE::mmix::private_signal_mask);
    libc_errno = EDOM;
    EXPECT_EQ(LIBC_NAMESPACE::injected_ppoll(fds, 2, &timeout, &mask),
              result < 0 ? -1 : int(result));
    EXPECT_EQ(seen_number, long(SYS_ppoll));
    EXPECT_EQ(seen_args[0], reinterpret_cast<long>(fds));
    EXPECT_EQ(seen_args[1], 2L);
    EXPECT_EQ(int(libc_errno), result < 0 ? int(-result) : EDOM);
    EXPECT_EQ(calls, 2U);
    EXPECT_TRUE(has_mask);
    EXPECT_EQ(seen_size, 8UL);
    EXPECT_EQ(seen_mask, ~LIBC_NAMESPACE::mmix::private_signal_mask);
    EXPECT_EQ(timeout.tv_sec, time_t(3));
    EXPECT_EQ(timeout.tv_nsec, 47L);
    for (auto word : mask.__signals)
      EXPECT_EQ(word, ~0UL);
  }
}

TEST(LlvmLibcMmixReadiness, InvalidTimeoutsNeverEnterWait) {
  calls = 0;
  const timespec invalid[] = {{-1, 0}, {0, -1}, {0, 1000000000}};
  for (const auto &timeout : invalid) {
    EXPECT_EQ(LIBC_NAMESPACE::injected_pselect(0, nullptr, nullptr, nullptr,
                                               &timeout, nullptr), -1);
    EXPECT_EQ(int(libc_errno), EINVAL);
    EXPECT_EQ(LIBC_NAMESPACE::injected_ppoll(nullptr, 0, &timeout, nullptr), -1);
    EXPECT_EQ(int(libc_errno), EINVAL);
  }
  timeval invalid_tv{0, -1};
  EXPECT_EQ(LIBC_NAMESPACE::injected_select(0, nullptr, nullptr, nullptr, &invalid_tv), -1);
  EXPECT_EQ(int(libc_errno), EINVAL);
  EXPECT_EQ(calls, 0U);
}
