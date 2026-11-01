//===-- Tests for bounded Linux AIO dispatch wake channel
//------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/linux/aio_dispatch_channel.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;

namespace {
constexpr int CONTROL_FD = 0; // Zero must be owned and closed normally.
constexpr int RING_FD = 17;

struct State {
  int eventfd_calls = 0, close_calls = 0, read_calls = 0, write_calls = 0;
  int ppoll_calls = 0;
  int eventfd_error = 0, close_error = 0, read_error = 0, write_error = 0;
  int ppoll_error = 0;
  int eventfd_result = CONTROL_FD, ppoll_result = 0;
  nfds_t expected_count = 2;
  size_t read_size = sizeof(uint64_t), write_size = sizeof(uint64_t);
  short control_events = 0, ring_events = 0;
  bool live = false, saw_timeout = false;
  timespec seen_timeout = {};
};

struct FakeOperations {
  State *state;

  ErrorOr<int> eventfd(unsigned value, int flags) {
    ++state->eventfd_calls;
    EXPECT_EQ(value, 0U);
    EXPECT_EQ(flags, O_NONBLOCK | O_CLOEXEC);
    EXPECT_FALSE(state->live);
    if (state->eventfd_error)
      return Error(state->eventfd_error);
    state->live = state->eventfd_result >= 0;
    return state->eventfd_result;
  }

  ErrorOr<int> close(int fd) {
    ++state->close_calls;
    EXPECT_EQ(fd, CONTROL_FD);
    EXPECT_TRUE(state->live);
    state->live = false;
    if (state->close_error)
      return Error(state->close_error);
    return 0;
  }

  ErrorOr<size_t> read(int fd, void *buffer, size_t size) {
    ++state->read_calls;
    EXPECT_EQ(fd, CONTROL_FD);
    EXPECT_TRUE(state->live);
    EXPECT_EQ(size, sizeof(uint64_t));
    *static_cast<uint64_t *>(buffer) = UINT64_MAX - 1;
    if (state->read_error)
      return Error(state->read_error);
    return state->read_size;
  }

  ErrorOr<size_t> write(int fd, const void *buffer, size_t size) {
    ++state->write_calls;
    EXPECT_EQ(fd, CONTROL_FD);
    EXPECT_TRUE(state->live);
    EXPECT_EQ(size, sizeof(uint64_t));
    EXPECT_EQ(*static_cast<const uint64_t *>(buffer), uint64_t(1));
    if (state->write_error)
      return Error(state->write_error);
    return state->write_size;
  }

  ErrorOr<int> ppoll(pollfd *fds, nfds_t count, timespec *timeout) {
    ++state->ppoll_calls;
    EXPECT_TRUE(state->live);
    EXPECT_EQ(count, state->expected_count);
    EXPECT_EQ(fds[0].fd, CONTROL_FD);
    if (count == 2)
      EXPECT_EQ(fds[1].fd, RING_FD);
    for (nfds_t i = 0; i < count; ++i) {
      EXPECT_EQ(int(fds[i].events), POLLIN);
      EXPECT_EQ(int(fds[i].revents), 0);
    }
    state->saw_timeout = timeout != nullptr;
    if (timeout) {
      state->seen_timeout = *timeout;
      *timeout = {}; // Model the raw syscall's timeout writeback.
    }
    fds[0].revents = state->control_events;
    if (count == 2)
      fds[1].revents = state->ring_events;
    if (state->ppoll_error)
      return Error(state->ppoll_error);
    return state->ppoll_result;
  }
};

using Channel = LIBC_NAMESPACE::aio_dispatch_channel::Channel<FakeOperations>;

void expect_error(const ErrorOr<void> &result, int error) {
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error);
}
} // namespace

TEST(LlvmLibcAioDispatchChannelTest, ControlOnlyMaintenanceWait) {
  State state;
  Channel channel({&state});
  expect_error(channel.wait_control(nullptr), EBADF);
  ASSERT_TRUE(channel.initialize().has_value());
  state.expected_count = 1;
  timespec timeout{0, 100000000};
  EXPECT_TRUE(channel.wait_control(&timeout).has_value());
  EXPECT_EQ(state.read_calls, 0);
  EXPECT_EQ(timeout.tv_nsec, 100000000L);
  state.ppoll_result = 1;
  state.control_events = POLLIN;
  EXPECT_TRUE(channel.wait_control(&timeout).has_value());
  EXPECT_EQ(state.read_calls, 1);
  state.ppoll_error = EINTR;
  expect_error(channel.wait_control(&timeout), EINTR);
  state.ppoll_error = 0;
  state.ppoll_result = 2;
  expect_error(channel.wait_control(&timeout), EPROTO);
  state.ppoll_result = 1;
  state.control_events = POLLNVAL;
  expect_error(channel.wait_control(&timeout), EBADF);
  EXPECT_EQ(state.read_calls, 1);
}

TEST(LlvmLibcAioDispatchChannelTest, ClosedOperationsHaveNoSideEffects) {
  State state;
  {
    Channel channel(FakeOperations{&state});
    EXPECT_EQ(channel.control_fd(), -1);
    expect_error(channel.wake(), EBADF);
    expect_error(channel.wait(RING_FD, nullptr), EBADF);
    EXPECT_TRUE(channel.reset().has_value());
    EXPECT_TRUE(channel.reset().has_value());
  }
  EXPECT_EQ(state.eventfd_calls, 0);
  EXPECT_EQ(state.close_calls, 0);
  EXPECT_EQ(state.write_calls, 0);
  EXPECT_EQ(state.read_calls, 0);
  EXPECT_EQ(state.ppoll_calls, 0);
}

TEST(LlvmLibcAioDispatchChannelTest, InitializeAndDestructorOwnOnlyControl) {
  State state;
  {
    Channel channel(FakeOperations{&state});
    ASSERT_TRUE(channel.initialize().has_value());
    EXPECT_EQ(channel.control_fd(), CONTROL_FD);
    state.close_error = EINTR;
    expect_error(channel.initialize(), EBUSY);
    EXPECT_EQ(state.eventfd_calls, 1);
    EXPECT_EQ(state.close_calls, 0);
    EXPECT_TRUE(state.live);
  }
  EXPECT_EQ(state.close_calls, 1);
  EXPECT_FALSE(state.live);
}

TEST(LlvmLibcAioDispatchChannelTest, CreationErrorsPreservedAndRetryable) {
  const int errors[] = {EINTR, EMFILE, ENFILE, ENOMEM, EINVAL};
  for (int error : errors) {
    State state;
    {
      Channel channel(FakeOperations{&state});
      state.eventfd_error = error;
      state.close_error = EIO;
      expect_error(channel.initialize(), error);
      EXPECT_EQ(channel.control_fd(), -1);
      EXPECT_EQ(state.eventfd_calls, 1);
      EXPECT_EQ(state.close_calls, 0);
      state.eventfd_error = 0;
      ASSERT_TRUE(channel.initialize().has_value());
    }
    EXPECT_EQ(state.close_calls, 1);
    EXPECT_FALSE(state.live);
  }
}

TEST(LlvmLibcAioDispatchChannelTest, MalformedCreationDoesNotOwnDescriptor) {
  State state;
  Channel channel(FakeOperations{&state});
  state.eventfd_result = -2;
  expect_error(channel.initialize(), EPROTO);
  EXPECT_EQ(channel.control_fd(), -1);
  EXPECT_TRUE(channel.reset().has_value());
  EXPECT_EQ(state.close_calls, 0);
  state.eventfd_result = CONTROL_FD;
  EXPECT_TRUE(channel.initialize().has_value());
}

TEST(LlvmLibcAioDispatchChannelTest, ResetConsumesCloseEvenOnError) {
  const int errors[] = {0, EINTR, EIO, EBADF};
  for (int error : errors) {
    State state;
    {
      Channel channel(FakeOperations{&state});
      ASSERT_TRUE(channel.initialize().has_value());
      state.close_error = error;
      auto result = channel.reset();
      if (error)
        expect_error(result, error);
      else
        EXPECT_TRUE(result.has_value());
      EXPECT_EQ(channel.control_fd(), -1);
      EXPECT_FALSE(state.live);
      EXPECT_TRUE(channel.reset().has_value());
      EXPECT_EQ(state.close_calls, 1);
      expect_error(channel.wake(), EBADF);
      expect_error(channel.wait(RING_FD, nullptr), EBADF);
      ASSERT_TRUE(channel.initialize().has_value());
    }
    EXPECT_EQ(state.close_calls, 2);
    EXPECT_FALSE(state.live);
  }
}

TEST(LlvmLibcAioDispatchChannelTest, WakeCoalescesAndNeverRetries) {
  State state;
  Channel channel(FakeOperations{&state});
  ASSERT_TRUE(channel.initialize().has_value());
  const int errors[] = {0, EAGAIN, EINTR, EBADF, EIO};
  for (int error : errors) {
    state.write_error = error;
    int calls = state.write_calls;
    auto result = channel.wake();
    if (!error || error == EAGAIN)
      EXPECT_TRUE(result.has_value());
    else
      expect_error(result, error);
    EXPECT_EQ(state.write_calls, calls + 1);
  }
  state.write_error = 0;
  const size_t sizes[] = {0, 1, 7, 9};
  for (size_t size : sizes) {
    state.write_size = size;
    int calls = state.write_calls;
    expect_error(channel.wake(), EPROTO);
    EXPECT_EQ(state.write_calls, calls + 1);
  }
  EXPECT_EQ(state.read_calls, 0);
  EXPECT_EQ(state.ppoll_calls, 0);
}

TEST(LlvmLibcAioDispatchChannelTest, InvalidRingRejectedBeforePoll) {
  State state;
  Channel channel(FakeOperations{&state});
  ASSERT_TRUE(channel.initialize().has_value());
  const int invalid[] = {-1, -2, CONTROL_FD};
  for (int fd : invalid)
    expect_error(channel.wait(fd, nullptr), EBADF);
  EXPECT_EQ(state.ppoll_calls, 0);
  EXPECT_EQ(state.read_calls, 0);
  EXPECT_EQ(state.close_calls, 0);
}

TEST(LlvmLibcAioDispatchChannelTest, TimeoutAndReadinessEachReturnOneStep) {
  State state;
  Channel channel(FakeOperations{&state});
  ASSERT_TRUE(channel.initialize().has_value());
  const timespec timeout = {3, 123456789};
  EXPECT_TRUE(channel.wait(RING_FD, &timeout).has_value());
  EXPECT_TRUE(state.saw_timeout);
  EXPECT_EQ(state.seen_timeout.tv_sec, timeout.tv_sec);
  EXPECT_EQ(state.seen_timeout.tv_nsec, timeout.tv_nsec);
  EXPECT_EQ(timeout.tv_sec, decltype(timeout.tv_sec)(3));
  EXPECT_EQ(timeout.tv_nsec, decltype(timeout.tv_nsec)(123456789));
  EXPECT_EQ(state.read_calls, 0);
  const timespec zero = {};
  EXPECT_TRUE(channel.wait(RING_FD, &zero).has_value());
  EXPECT_TRUE(state.saw_timeout);
  EXPECT_EQ(state.seen_timeout.tv_sec, decltype(zero.tv_sec)(0));
  EXPECT_EQ(state.seen_timeout.tv_nsec, decltype(zero.tv_nsec)(0));

  state.ppoll_result = 1;
  state.ring_events = POLLIN;
  EXPECT_TRUE(channel.wait(RING_FD, nullptr).has_value());
  EXPECT_FALSE(state.saw_timeout);
  EXPECT_EQ(state.read_calls, 0);
  state.ring_events = 0;
  state.control_events = POLLIN;
  EXPECT_TRUE(channel.wait(RING_FD, nullptr).has_value());
  EXPECT_EQ(state.read_calls, 1);
  state.ppoll_result = 2;
  state.ring_events = POLLIN;
  EXPECT_TRUE(channel.wait(RING_FD, nullptr).has_value());
  EXPECT_EQ(state.read_calls, 2);
  EXPECT_EQ(state.ppoll_calls, 5);
  EXPECT_EQ(state.write_calls, 0);
  EXPECT_EQ(state.close_calls, 0);
}

TEST(LlvmLibcAioDispatchChannelTest, PollErrorsPropagateWithoutReadOrRetry) {
  State state;
  Channel channel(FakeOperations{&state});
  ASSERT_TRUE(channel.initialize().has_value());
  state.control_events = POLLIN;
  const int errors[] = {EINTR, EINVAL, EBADF, ENOMEM, EAGAIN, EIO};
  for (int error : errors) {
    state.ppoll_error = error;
    const timespec timeout = {2, 42};
    int calls = state.ppoll_calls;
    expect_error(channel.wait(RING_FD, &timeout), error);
    EXPECT_EQ(state.ppoll_calls, calls + 1);
    EXPECT_EQ(timeout.tv_sec, decltype(timeout.tv_sec)(2));
    EXPECT_EQ(timeout.tv_nsec, decltype(timeout.tv_nsec)(42));
  }
  state.ppoll_error = 0;
  const int invalid[] = {-1, 3};
  for (int result : invalid) {
    state.ppoll_result = result;
    expect_error(channel.wait(RING_FD, nullptr), EPROTO);
  }
  EXPECT_EQ(state.read_calls, 0);
}

TEST(LlvmLibcAioDispatchChannelTest, PollFlagsOverrideReadinessOnEitherFd) {
  const short flags[] = {POLLNVAL, POLLERR, POLLHUP, POLLERR | POLLHUP,
                         POLLNVAL | POLLERR | POLLHUP};
  for (short flag : flags) {
    for (int index = 0; index < 2; ++index) {
      State state;
      Channel channel(FakeOperations{&state});
      ASSERT_TRUE(channel.initialize().has_value());
      state.ppoll_result = 2;
      state.control_events = POLLIN | (index == 0 ? flag : 0);
      state.ring_events = POLLIN | (index == 1 ? flag : 0);
      expect_error(channel.wait(RING_FD, nullptr),
                   flag & POLLNVAL ? EBADF : EIO);
      EXPECT_EQ(state.ppoll_calls, 1);
      EXPECT_EQ(state.read_calls, 0);
    }
  }
  State state;
  Channel channel(FakeOperations{&state});
  ASSERT_TRUE(channel.initialize().has_value());
  state.ppoll_result = 2;
  state.control_events = POLLERR;
  state.ring_events = POLLNVAL;
  expect_error(channel.wait(RING_FD, nullptr), EBADF);
}

TEST(LlvmLibcAioDispatchChannelTest, ControlReadIsBoundedAndChecksErrors) {
  State state;
  Channel channel(FakeOperations{&state});
  ASSERT_TRUE(channel.initialize().has_value());
  state.ppoll_result = 2;
  state.control_events = state.ring_events = POLLIN;
  const int errors[] = {0, EAGAIN, EINTR, EBADF, EIO};
  for (int error : errors) {
    state.read_error = error;
    int reads = state.read_calls, polls = state.ppoll_calls;
    auto result = channel.wait(RING_FD, nullptr);
    if (!error || error == EAGAIN)
      EXPECT_TRUE(result.has_value());
    else
      expect_error(result, error);
    EXPECT_EQ(state.read_calls, reads + 1);
    EXPECT_EQ(state.ppoll_calls, polls + 1);
  }
  state.read_error = 0;
  const size_t sizes[] = {0, 1, 7, 9};
  for (size_t size : sizes) {
    state.read_size = size;
    int reads = state.read_calls, polls = state.ppoll_calls;
    expect_error(channel.wait(RING_FD, nullptr), EPROTO);
    EXPECT_EQ(state.read_calls, reads + 1);
    EXPECT_EQ(state.ppoll_calls, polls + 1);
  }
  EXPECT_EQ(state.close_calls, 0);
  EXPECT_EQ(state.write_calls, 0);
}
