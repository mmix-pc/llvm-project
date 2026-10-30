//===-- Tests for the private Linux AIO wait event ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/linux/aio_wait_event.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"

#include <pthread.h>
#include <sched.h>
#include <time.h>

using LIBC_NAMESPACE::AioWaitEvent;
using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;
using LIBC_NAMESPACE::Futex;
using LIBC_NAMESPACE::FutexWordType;
using LIBC_NAMESPACE::cpp::MemoryOrder;

namespace {
struct ErrnoGuard {
  int saved = libc_errno;
  ErrnoGuard() { libc_errno = EDOM; }
  ~ErrnoGuard() { libc_errno = saved; }
  void check() { EXPECT_EQ(int(libc_errno), EDOM); }
};

struct Wake {
  unsigned calls = 0;
  int result = 0;

  static ErrorOr<int> run(void *context, Futex &word) {
    auto &self = *static_cast<Wake *>(context);
    ++self.calls;
    EXPECT_EQ(word.load(MemoryOrder::ACQUIRE), FutexWordType(1));
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&word) % 8, uintptr_t(0));
    if (self.result < 0)
      return Error(-self.result);
    return self.result;
  }
};

struct Step {
  int result;
  bool publish = false;
};

struct Script {
  AioWaitEvent &event;
  const Step *steps;
  unsigned count;
  unsigned calls = 0;
  Wake wake{};
  const timespec *deadline = nullptr;
  const void *first_timeout = nullptr;
  Futex *first_word = nullptr;

  static int run(void *context, Futex *word, int operation,
                 FutexWordType expected, const void *timeout, unsigned bitset) {
    auto &self = *static_cast<Script *>(context);
    EXPECT_EQ(operation, FUTEX_WAIT_BITSET_PRIVATE);
    EXPECT_EQ(expected, FutexWordType(0));
    EXPECT_EQ(bitset, unsigned(FUTEX_BITSET_MATCH_ANY));
    EXPECT_EQ(word->load(MemoryOrder::ACQUIRE), FutexWordType(0));
    EXPECT_EQ(reinterpret_cast<uintptr_t>(word) % 8, uintptr_t(0));
    if (!self.calls) {
      self.first_word = word;
      self.first_timeout = timeout;
    }
    EXPECT_EQ(word, self.first_word);
    EXPECT_EQ(timeout, self.first_timeout);
    EXPECT_EQ(timeout != nullptr, self.deadline != nullptr);
    if (timeout && self.deadline) {
#if defined(SYS_futex_time64)
      const auto &absolute = *static_cast<const __kernel_timespec *>(timeout);
#else
      const auto &absolute = *static_cast<const timespec *>(timeout);
#endif
      EXPECT_EQ(int64_t(absolute.tv_sec), int64_t(self.deadline->tv_sec));
      EXPECT_EQ(int64_t(absolute.tv_nsec), int64_t(self.deadline->tv_nsec));
    }
    // Exhaustion fails finitely rather than hanging a broken retry loop.
    if (self.calls >= self.count) {
      EXPECT_LT(self.calls, self.count);
      return -EIO;
    }
    const Step &step = self.steps[self.calls++];
    if (step.publish)
      self.event.publish(Wake::run, &self.wake);
    return step.result;
  }
};
} // namespace

TEST(LlvmLibcAioWaitEventTest, CompletionBeforeWaitIsLatched) {
  ErrnoGuard guard;
  AioWaitEvent event;
  Script script{event, nullptr, 0};
  EXPECT_FALSE(event.ready());
  event.publish(Wake::run, &script.wake);
  EXPECT_TRUE(event.ready());
  EXPECT_EQ(event.wait(nullptr, Script::run, &script), 0);
  EXPECT_EQ(event.wait(nullptr, Script::run, &script), 0);
  EXPECT_EQ(script.calls, 0U);
  EXPECT_EQ(script.wake.calls, 1U);
  guard.check();
}

TEST(LlvmLibcAioWaitEventTest, ZeroOrOneWokenIsSuccessAndNeverResets) {
  for (int count = 0; count != 2; ++count) {
    ErrnoGuard guard;
    AioWaitEvent event;
    Wake wake;
    wake.result = count;
    event.publish(Wake::run, &wake);
    EXPECT_TRUE(event.ready());
    event.publish(Wake::run, &wake);
    EXPECT_TRUE(event.ready());
    EXPECT_EQ(wake.calls, 2U);
    guard.check();
  }
}

TEST(LlvmLibcAioWaitEventTest, CompletionDuringSyscallWins) {
  const int results[] = {0, -EAGAIN, -EINTR, -ETIMEDOUT, -EFAULT, -EIO};
  for (int result : results) {
    ErrnoGuard guard;
    AioWaitEvent event;
    const Step steps[] = {{result, true}};
    Script script{event, steps, 1};
    EXPECT_EQ(event.wait(nullptr, Script::run, &script), 0);
    EXPECT_TRUE(event.ready());
    EXPECT_EQ(script.calls, 1U);
    EXPECT_EQ(script.wake.calls, 1U);
    guard.check();
  }
}

TEST(LlvmLibcAioWaitEventTest, SpuriousWakeAndEagainRetryUntilCompletion) {
  ErrnoGuard guard;
  AioWaitEvent event;
  const Step steps[] = {{0}, {-EAGAIN}, {0}, {-EAGAIN, true}};
  Script script{event, steps, 4};
  EXPECT_EQ(event.wait(nullptr, Script::run, &script), 0);
  EXPECT_EQ(script.calls, 4U);
  guard.check();
}

TEST(LlvmLibcAioWaitEventTest, ErrorsArePositiveAndDoNotLatchCompletion) {
  const int errors[] = {EINTR, ETIMEDOUT, EFAULT, EINVAL, ENOSYS, EIO};
  for (int error : errors) {
    ErrnoGuard guard;
    AioWaitEvent event;
    const Step steps[] = {{-error}};
    Script script{event, steps, 1};
    EXPECT_EQ(event.wait(nullptr, Script::run, &script),
              error == ETIMEDOUT ? EAGAIN : error);
    EXPECT_FALSE(event.ready());
    EXPECT_EQ(script.calls, 1U);
    EXPECT_EQ(script.wake.calls, 0U);
    // Reporting an error neither resets nor disables this one-shot event.
    event.publish(Wake::run, &script.wake);
    EXPECT_EQ(event.wait(nullptr, Script::run, &script), 0);
    EXPECT_EQ(script.calls, 1U);
    guard.check();
  }
}

TEST(LlvmLibcAioWaitEventTest, RetriesPreserveAbsoluteMonotonicDeadline) {
  ErrnoGuard guard;
  AioWaitEvent event;
  const timespec deadline{1234567, 999999999};
  const Step steps[] = {{-EAGAIN}, {0}, {-EAGAIN}, {0}, {-ETIMEDOUT}};
  Script script{event, steps, 5};
  script.deadline = &deadline;
  EXPECT_EQ(event.wait(&deadline, Script::run, &script), EAGAIN);
  EXPECT_EQ(script.calls, 5U);
  EXPECT_EQ(deadline.tv_sec, time_t(1234567));
  EXPECT_EQ(deadline.tv_nsec, 999999999L);
  EXPECT_FALSE(event.ready());
  guard.check();
}

TEST(LlvmLibcAioWaitEventTest, InvalidNanosecondsNeverReachSyscall) {
  const long nanoseconds[] = {-1, 1000000000};
  for (long ns : nanoseconds) {
    ErrnoGuard guard;
    AioWaitEvent event;
    Script script{event, nullptr, 0};
    timespec deadline{42, ns};
    EXPECT_EQ(event.wait(&deadline, Script::run, &script), EINVAL);
    EXPECT_EQ(script.calls, 0U);
    EXPECT_FALSE(event.ready());
    event.publish(Wake::run, &script.wake);
    EXPECT_EQ(event.wait(&deadline, Script::run, &script), 0);
    EXPECT_EQ(script.calls, 0U);
    guard.check();
  }
}

TEST(LlvmLibcAioWaitEventTest, BeforeEpochIsExpiredUnlessComplete) {
  ErrnoGuard guard;
  AioWaitEvent event;
  Script script{event, nullptr, 0};
  const timespec deadline{-1, 0};
  EXPECT_EQ(event.wait(&deadline, Script::run, &script), EAGAIN);
  EXPECT_EQ(script.calls, 0U);
  event.publish(Wake::run, &script.wake);
  EXPECT_EQ(event.wait(&deadline, Script::run, &script), 0);
  guard.check();
}

TEST(LlvmLibcAioWaitEventTest, WakeFailureCannotReturn) {
  const int errors[] = {EINTR, EFAULT, EINVAL, ENOSYS};
  for (int error : errors) {
    EXPECT_EXITS(([error] {
                   AioWaitEvent event;
                   Wake wake;
                   wake.result = -error;
                   event.publish(Wake::run, &wake);
                 }),
                 127);
  }
}

TEST(LlvmLibcAioWaitEventTest, NativeExpiredDeadlineAndWakePreserveErrno) {
  ErrnoGuard guard;
  AioWaitEvent event;
  const timespec deadline{0, 0};
  EXPECT_EQ(event.wait(&deadline), EAGAIN);
  EXPECT_FALSE(event.ready());
  guard.check();
  event.publish();
  EXPECT_EQ(event.wait(), 0);
  guard.check();
}

namespace {
struct ThreadWait {
  AioWaitEvent event;
  // Each subword atomic in this native probe also owns a complete octa.
  alignas(8) LIBC_NAMESPACE::cpp::Atomic<Futex *> word{nullptr};
  alignas(8) LIBC_NAMESPACE::cpp::Atomic<uint64_t> done{0};
  timespec deadline{};
  int result = -1;
  int payload = 0;
  int observed = 0;

  static int wait(void *context, Futex *word, int operation,
                  FutexWordType expected, const void *timeout,
                  unsigned bitset) {
    auto &self = *static_cast<ThreadWait *>(context);
    self.word.store(word, MemoryOrder::RELEASE);
    return LIBC_NAMESPACE::syscall_impl<int>(LIBC_NAMESPACE::FUTEX_SYSCALL_ID,
                                             word, operation, expected, timeout,
                                             nullptr, bitset);
  }
  static void *run(void *context) {
    auto &self = *static_cast<ThreadWait *>(context);
    self.result = self.event.wait(&self.deadline, wait, &self);
    if (self.result == 0)
      self.observed = self.payload;
    self.done.store(1, MemoryOrder::RELEASE);
    return nullptr;
  }
};
} // namespace

TEST(LlvmLibcAioWaitEventTest,
     NativeBlockedPthreadWakesAndAcquiresPublication) {
  ThreadWait state;
  ASSERT_EQ(::clock_gettime(CLOCK_MONOTONIC, &state.deadline), 0);
  state.deadline.tv_sec += 5;
  pthread_t thread;
  ASSERT_EQ(::pthread_create(&thread, nullptr, ThreadWait::run, &state), 0);

  bool blocked = false;
  int wake_error = 0;
  while (!state.done.load(MemoryOrder::ACQUIRE)) {
    Futex *word = state.word.load(MemoryOrder::ACQUIRE);
    if (word) {
      // A wake count of one proves actual kernel blocking, without sleeps.
      // This wake is deliberately spurious; publish below supplies completion.
      auto woke = word->notify_one();
      if (!woke) {
        wake_error = woke.error();
        break;
      }
      if (*woke == 1) {
        blocked = true;
        break;
      }
    }
    ::sched_yield();
  }
  state.payload = 42;
  state.event.publish();
  // Do not leave a live thread holding stack storage on an assertion failure.
  ASSERT_EQ(::pthread_join(thread, nullptr), 0);
  EXPECT_EQ(wake_error, 0);
  EXPECT_TRUE(blocked);
  EXPECT_EQ(state.result, 0);
  EXPECT_EQ(state.observed, 42);
}
