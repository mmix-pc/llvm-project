//===-- Tests for private Linux notification channels ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/linux/notification_channels.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;
namespace transport = LIBC_NAMESPACE::notification_transport;

namespace {
constexpr int CONTROL_FD = 0; // Zero is an owned fd, not a closed sentinel.
constexpr int TIMER_FD = 17;
constexpr int MQ_FD = 29;
constexpr int SIGNAL = 34;
constexpr int TIMER_ID = 73;
constexpr uint64_t TOKEN = UINT64_C(0x8123456789abcdef);
constexpr transport::Notification SENTINEL = {
    42, transport::Event::TimerExpired, 19};

struct State {
  int eventfd_calls = 0, signalfd_calls = 0, socket_calls = 0;
  int read_calls = 0, write_calls = 0, recvmsg_calls = 0, close_calls = 0;
  int eventfd_error = 0, signalfd_error = 0, socket_error = 0;
  int read_error = 0, write_error = 0, recvmsg_error = 0;
  int close_errors[3] = {};
  int closed[12] = {};
  bool live[3] = {};
  int eventfd_result = CONTROL_FD, signalfd_result = TIMER_FD;
  int socket_result = MQ_FD;
  uint64_t mask = 0;
  uint64_t control_value = UINT64_MAX - 1;
  size_t read_size = 0;
  size_t write_size = sizeof(uint64_t);
  size_t mq_size = transport::MQ_COOKIE_SIZE;
  size_t source_size = sizeof(sockaddr_nl);
  int message_flags = 0;
  // Independent wire fixture, including a high-bit integer token.
  unsigned char cookie[32] = {0x81, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
  sockaddr_nl source = {};
  signalfd_siginfo info = {};

  State() {
    source.nl_family = AF_NETLINK;
    cookie[31] = transport::MQ_WOKENUP;
    info.ssi_signo = SIGNAL;
    info.ssi_code = SI_TIMER;
    info.ssi_tid = TIMER_ID;
    info.ssi_ptr = TOKEN;
    info.ssi_overrun = 7;
    info.ssi_pid = 12345;
    info.ssi_int = 99;
  }
};

int fd_index(int fd) {
  if (fd == CONTROL_FD)
    return 0;
  if (fd == TIMER_FD)
    return 1;
  EXPECT_EQ(fd, MQ_FD);
  return 2;
}

struct FakeOperations {
  State *state;

  ErrorOr<int> eventfd(unsigned value, int flags) {
    ++state->eventfd_calls;
    EXPECT_EQ(value, 0U);
    EXPECT_EQ(flags, O_NONBLOCK | O_CLOEXEC);
    if (state->eventfd_error)
      return Error(state->eventfd_error);
    if (state->eventfd_result >= 0) {
      EXPECT_FALSE(state->live[0]);
      state->live[0] = true;
    }
    return state->eventfd_result;
  }

  ErrorOr<int> signalfd(int fd, const uint64_t *mask, size_t size, int flags) {
    ++state->signalfd_calls;
    EXPECT_EQ(fd, -1);
    EXPECT_EQ(size, size_t(8));
    EXPECT_EQ(flags, SFD_NONBLOCK | SFD_CLOEXEC);
    state->mask = *mask;
    EXPECT_TRUE(state->live[0]);
    if (state->signalfd_error)
      return Error(state->signalfd_error);
    if (state->signalfd_result >= 0) {
      EXPECT_FALSE(state->live[1]);
      state->live[1] = true;
    }
    return state->signalfd_result;
  }

  ErrorOr<int> socket(int domain, int type, int protocol) {
    ++state->socket_calls;
    EXPECT_EQ(domain, AF_NETLINK);
    EXPECT_EQ(type, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC);
    EXPECT_EQ(protocol, NETLINK_ROUTE);
    EXPECT_EQ(protocol, 0);
    EXPECT_TRUE(state->live[0]);
    EXPECT_TRUE(state->live[1]);
    if (state->socket_error)
      return Error(state->socket_error);
    if (state->socket_result >= 0) {
      EXPECT_FALSE(state->live[2]);
      state->live[2] = true;
    }
    return state->socket_result;
  }

  ErrorOr<int> close(int fd) {
    int index = fd_index(fd);
    EXPECT_TRUE(state->live[index]);
    state->live[index] = false;
    EXPECT_LT(state->close_calls, 12);
    if (state->close_calls < 12)
      state->closed[state->close_calls] = fd;
    ++state->close_calls;
    if (state->close_errors[index])
      return Error(state->close_errors[index]);
    return 0;
  }

  ErrorOr<size_t> read(int fd, void *buffer, size_t size) {
    ++state->read_calls;
    EXPECT_TRUE(state->live[fd_index(fd)]);
    // Deliberately write even on syscall failure to exercise output staging.
    if (fd == CONTROL_FD) {
      EXPECT_EQ(size, size_t(8));
      *static_cast<uint64_t *>(buffer) = state->control_value;
    } else {
      EXPECT_EQ(fd, TIMER_FD);
      EXPECT_EQ(size, size_t(128));
      *static_cast<signalfd_siginfo *>(buffer) = state->info;
    }
    if (state->read_error)
      return Error(state->read_error);
    return state->read_size;
  }

  ErrorOr<size_t> write(int fd, const void *buffer, size_t size) {
    ++state->write_calls;
    EXPECT_EQ(fd, CONTROL_FD);
    EXPECT_TRUE(state->live[0]);
    EXPECT_EQ(size, size_t(8));
    EXPECT_EQ(*static_cast<const uint64_t *>(buffer), uint64_t(1));
    if (state->write_error)
      return Error(state->write_error);
    return state->write_size;
  }

  ErrorOr<size_t> recvmsg(int fd, msghdr *message, int flags) {
    ++state->recvmsg_calls;
    EXPECT_EQ(fd, MQ_FD);
    EXPECT_TRUE(state->live[2]);
    EXPECT_EQ(flags, static_cast<int>(MSG_DONTWAIT));
    EXPECT_EQ(message->msg_namelen, socklen_t(sizeof(sockaddr_nl)));
    EXPECT_EQ(message->msg_iovlen, size_t(1));
    EXPECT_EQ(message->msg_iov[0].iov_len, size_t(32));
    EXPECT_TRUE(message->msg_control == nullptr);
    EXPECT_EQ(message->msg_controllen, size_t(0));
    EXPECT_EQ(message->msg_flags, 0);
    auto *bytes = static_cast<unsigned char *>(message->msg_iov[0].iov_base);
    for (size_t i = 0; i < sizeof(state->cookie); ++i)
      bytes[i] = state->cookie[i];
    *static_cast<sockaddr_nl *>(message->msg_name) = state->source;
    message->msg_namelen = static_cast<socklen_t>(state->source_size);
    message->msg_flags = state->message_flags;
    if (state->recvmsg_error)
      return Error(state->recvmsg_error);
    return state->mq_size;
  }
};

using Channels =
    LIBC_NAMESPACE::notification_channels::Channels<FakeOperations>;

template <class T> void expect_error(const ErrorOr<T> &result, int error) {
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error);
}

void expect_closed(const Channels &channels, const State &state) {
  EXPECT_EQ(channels.control_fd(), -1);
  EXPECT_EQ(channels.timer_fd(), -1);
  EXPECT_EQ(channels.mq_fd(), -1);
  for (bool live : state.live)
    EXPECT_FALSE(live);
}

void reject_control(Channels &channels, State &state, int error) {
  uint64_t output = TOKEN;
  int calls = state.read_calls;
  expect_error(channels.read_control(output), error);
  EXPECT_EQ(output, TOKEN);
  EXPECT_EQ(state.read_calls, calls + (channels.control_fd() < 0 ? 0 : 1));
}

void reject_timer(Channels &channels, State &state, int error) {
  signalfd_siginfo output;
  auto *bytes = reinterpret_cast<unsigned char *>(&output);
  for (size_t i = 0; i < sizeof(output); ++i)
    bytes[i] = 0xa5;
  int calls = state.read_calls;
  expect_error(channels.read_timer(output), error);
  for (size_t i = 0; i < sizeof(output); ++i)
    EXPECT_EQ(bytes[i], static_cast<unsigned char>(0xa5));
  EXPECT_EQ(state.read_calls, calls + (channels.timer_fd() < 0 ? 0 : 1));
}

void reject_mq(Channels &channels, State &state, int error) {
  auto output = SENTINEL;
  int calls = state.recvmsg_calls;
  expect_error(channels.read_mq(output), error);
  EXPECT_EQ(output.token, SENTINEL.token);
  EXPECT_TRUE(output.event == SENTINEL.event);
  EXPECT_EQ(output.overrun, SENTINEL.overrun);
  EXPECT_EQ(state.recvmsg_calls, calls + (channels.mq_fd() < 0 ? 0 : 1));
}
} // namespace

TEST(LlvmLibcNotificationChannelsTest, ClosedOperationsHaveNoSideEffects) {
  State state;
  {
    Channels channels(FakeOperations{&state});
    expect_closed(channels, state);
    expect_error(channels.ensure_mq(), EBADF);
    expect_error(channels.wake(), EBADF);
    reject_control(channels, state, EBADF);
    reject_timer(channels, state, EBADF);
    reject_mq(channels, state, EBADF);
    EXPECT_TRUE(channels.reset().has_value());
    EXPECT_TRUE(channels.reset().has_value());
  }
  EXPECT_EQ(state.eventfd_calls, 0);
  EXPECT_EQ(state.signalfd_calls, 0);
  EXPECT_EQ(state.socket_calls, 0);
  EXPECT_EQ(state.write_calls, 0);
  EXPECT_EQ(state.close_calls, 0);
}

TEST(LlvmLibcNotificationChannelsTest, SignalValidationAndKernelMask) {
  State state;
  Channels channels(FakeOperations{&state});
  const int invalid[] = {-1, 0, SIGKILL, SIGSTOP, 65, INT32_MAX};
  for (int signal : invalid)
    expect_error(channels.initialize(signal), EINVAL);
  EXPECT_EQ(state.eventfd_calls, 0);
  EXPECT_EQ(state.signalfd_calls, 0);

  const int valid[] = {1, 32, SIGNAL, 64};
  for (int signal : valid) {
    ASSERT_TRUE(channels.initialize(signal).has_value());
    EXPECT_EQ(state.mask, uint64_t(1) << (signal - 1));
    EXPECT_EQ(channels.control_fd(), CONTROL_FD);
    EXPECT_EQ(channels.timer_fd(), TIMER_FD);
    EXPECT_EQ(channels.mq_fd(), -1);
    EXPECT_TRUE(channels.reset().has_value());
    expect_closed(channels, state);
  }
  EXPECT_EQ(state.socket_calls, 0);
  EXPECT_EQ(state.close_calls, 8);
}

TEST(LlvmLibcNotificationChannelsTest, RepeatedInitializeAndLazyMq) {
  State state;
  {
    Channels channels(FakeOperations{&state});
    ASSERT_TRUE(channels.initialize(SIGNAL).has_value());
    expect_error(channels.initialize(SIGNAL + 1), EBUSY);
    expect_error(channels.initialize(0), EBUSY);
    EXPECT_EQ(state.eventfd_calls, 1);
    EXPECT_EQ(state.signalfd_calls, 1);
    EXPECT_EQ(state.mask, uint64_t(1) << (SIGNAL - 1));
    reject_mq(channels, state, EBADF);
    EXPECT_EQ(state.socket_calls, 0);
    ASSERT_TRUE(channels.ensure_mq().has_value());
    EXPECT_EQ(channels.mq_fd(), MQ_FD);
    ASSERT_TRUE(channels.ensure_mq().has_value());
    EXPECT_EQ(state.socket_calls, 1);
    expect_error(channels.initialize(SIGNAL), EBUSY);
    EXPECT_EQ(state.close_calls, 0);
  }
  EXPECT_EQ(state.close_calls, 3);
  EXPECT_EQ(state.closed[0], MQ_FD);
  EXPECT_EQ(state.closed[1], TIMER_FD);
  EXPECT_EQ(state.closed[2], CONTROL_FD);
  for (bool live : state.live)
    EXPECT_FALSE(live);
}

TEST(LlvmLibcNotificationChannelsTest, InitializationFailureRollbackAndRetry) {
  const int errors[] = {EINTR, EAGAIN, EMFILE, ENOSYS};
  for (int error : errors) {
    for (int stage = 0; stage < 2; ++stage) {
      State state;
      if (stage == 0)
        state.eventfd_error = error;
      else
        state.signalfd_error = error;
      state.close_errors[0] = EIO;
      {
        Channels channels(FakeOperations{&state});
        expect_error(channels.initialize(SIGNAL), error);
        expect_closed(channels, state);
        EXPECT_EQ(state.eventfd_calls, 1);
        EXPECT_EQ(state.signalfd_calls, stage);
        EXPECT_EQ(state.socket_calls, 0);
        EXPECT_EQ(state.close_calls, stage);
        EXPECT_TRUE(channels.reset().has_value());
        EXPECT_EQ(state.close_calls, stage);
        state.eventfd_error = state.signalfd_error = 0;
        state.close_errors[0] = 0;
        ASSERT_TRUE(channels.initialize(SIGNAL).has_value());
      }
      EXPECT_EQ(state.close_calls, stage + 2);
    }
  }
}

TEST(LlvmLibcNotificationChannelsTest, LazyMqFailurePreservesExistingChannels) {
  const int errors[] = {EINTR, EAGAIN, EMFILE, EPROTONOSUPPORT};
  for (int error : errors) {
    State state;
    Channels channels(FakeOperations{&state});
    ASSERT_TRUE(channels.initialize(SIGNAL).has_value());
    state.socket_error = error;
    expect_error(channels.ensure_mq(), error);
    EXPECT_EQ(state.socket_calls, 1);
    EXPECT_EQ(channels.mq_fd(), -1);
    EXPECT_EQ(channels.control_fd(), CONTROL_FD);
    EXPECT_EQ(channels.timer_fd(), TIMER_FD);
    EXPECT_EQ(state.close_calls, 0);
    ASSERT_TRUE(channels.wake().has_value());
    state.socket_error = 0;
    ASSERT_TRUE(channels.ensure_mq().has_value());
    EXPECT_EQ(state.socket_calls, 2);
    ASSERT_TRUE(channels.reset().has_value());
    EXPECT_EQ(state.close_calls, 3);
  }
}

TEST(LlvmLibcNotificationChannelsTest, InvalidSuccessfulCreationResults) {
  for (int stage = 0; stage < 3; ++stage) {
    State state;
    Channels channels(FakeOperations{&state});
    if (stage == 0)
      state.eventfd_result = -1;
    if (stage == 1)
      state.signalfd_result = -1;
    if (stage < 2) {
      expect_error(channels.initialize(SIGNAL), EPROTO);
      expect_closed(channels, state);
      EXPECT_EQ(state.close_calls, stage);
    } else {
      ASSERT_TRUE(channels.initialize(SIGNAL).has_value());
      state.socket_result = -1;
      expect_error(channels.ensure_mq(), EPROTO);
      EXPECT_EQ(channels.mq_fd(), -1);
      EXPECT_EQ(state.close_calls, 0);
    }
  }
}

TEST(LlvmLibcNotificationChannelsTest, CloseErrorsConsumeAllFdsOnlyOnce) {
  // Every failing position, and multiple failures with first-error reporting.
  for (int failure = 0; failure < 4; ++failure) {
    State state;
    {
      Channels channels(FakeOperations{&state});
      ASSERT_TRUE(channels.initialize(SIGNAL).has_value());
      ASSERT_TRUE(channels.ensure_mq().has_value());
      for (int i = 0; i < 3; ++i)
        if (i == failure || failure == 3)
          state.close_errors[i] = i == 2 ? EINTR : EIO;
      expect_error(channels.reset(), failure >= 2 ? EINTR : EIO);
      expect_closed(channels, state);
      EXPECT_EQ(state.close_calls, 3);
      EXPECT_EQ(state.closed[0], MQ_FD);
      EXPECT_EQ(state.closed[1], TIMER_FD);
      EXPECT_EQ(state.closed[2], CONTROL_FD);
      ASSERT_TRUE(channels.reset().has_value());
      EXPECT_EQ(state.close_calls, 3);
      reject_control(channels, state, EBADF);
      reject_timer(channels, state, EBADF);
      reject_mq(channels, state, EBADF);
      expect_error(channels.wake(), EBADF);
      expect_error(channels.ensure_mq(), EBADF);
      // Reusing numeric fd values is safe: only the new ownership is closed.
      ASSERT_TRUE(channels.initialize(SIGNAL).has_value());
      ASSERT_TRUE(channels.ensure_mq().has_value());
    }
    EXPECT_EQ(state.close_calls, 6);
    EXPECT_EQ(state.closed[3], MQ_FD);
    EXPECT_EQ(state.closed[4], TIMER_FD);
    EXPECT_EQ(state.closed[5], CONTROL_FD);
  }
}

TEST(LlvmLibcNotificationChannelsTest, WakeOneAttemptAndSaturation) {
  State state;
  Channels channels(FakeOperations{&state});
  ASSERT_TRUE(channels.initialize(SIGNAL).has_value());
  ASSERT_TRUE(channels.wake().has_value());
  EXPECT_EQ(state.write_calls, 1);
  state.write_error = EAGAIN;
  ASSERT_TRUE(channels.wake().has_value());
  EXPECT_EQ(state.write_calls, 2);
  const int errors[] = {EINTR, EIO, EBADF};
  for (int error : errors) {
    state.write_error = error;
    int calls = state.write_calls;
    expect_error(channels.wake(), error);
    EXPECT_EQ(state.write_calls, calls + 1);
  }
  state.write_error = 0;
  const size_t invalid[] = {0, 1, 7, 9, SIZE_MAX};
  for (size_t size : invalid) {
    state.write_size = size;
    int calls = state.write_calls;
    expect_error(channels.wake(), EPROTO);
    EXPECT_EQ(state.write_calls, calls + 1);
  }
  EXPECT_EQ(state.read_calls, 0);
}

TEST(LlvmLibcNotificationChannelsTest, ControlExactReadAndUnchangedOutput) {
  State state;
  Channels channels(FakeOperations{&state});
  ASSERT_TRUE(channels.initialize(SIGNAL).has_value());
  for (size_t size = 0; size < 8; ++size) {
    state.read_size = size;
    reject_control(channels, state, EPROTO);
  }
  state.read_size = 9;
  reject_control(channels, state, EPROTO);
  state.read_size = SIZE_MAX;
  reject_control(channels, state, EPROTO);
  const int errors[] = {EINTR, EAGAIN, EIO, EBADF};
  for (int error : errors) {
    state.read_error = error;
    reject_control(channels, state, error);
  }
  state.read_error = 0;
  state.read_size = 8;
  uint64_t output = 0;
  int calls = state.read_calls;
  ASSERT_TRUE(channels.read_control(output).has_value());
  EXPECT_EQ(output, state.control_value);
  EXPECT_EQ(state.read_calls, calls + 1);
}

TEST(LlvmLibcNotificationChannelsTest, TimerExactReadAndUnchangedOutput) {
  State state;
  Channels channels(FakeOperations{&state});
  ASSERT_TRUE(channels.initialize(SIGNAL).has_value());
  for (size_t size = 0; size < 128; ++size) {
    state.read_size = size;
    reject_timer(channels, state, EPROTO);
  }
  const size_t oversized[] = {129, 256, SIZE_MAX};
  for (size_t size : oversized) {
    state.read_size = size;
    reject_timer(channels, state, EPROTO);
  }
  const int errors[] = {EINTR, EAGAIN, EIO, EBADF};
  for (int error : errors) {
    state.read_error = error;
    reject_timer(channels, state, error);
  }
}

TEST(LlvmLibcNotificationChannelsTest, TimerRawRecordRetainsRegistryIdentity) {
  State state;
  Channels channels(FakeOperations{&state});
  ASSERT_TRUE(channels.initialize(SIGNAL).has_value());
  state.read_size = 128;
  signalfd_siginfo output = {};
  ASSERT_TRUE(channels.read_timer(output).has_value());
  EXPECT_EQ(state.read_calls, 1);
  EXPECT_EQ(output.ssi_ptr, __u64(TOKEN));
  EXPECT_EQ(output.ssi_tid, __u32(TIMER_ID));
  EXPECT_EQ(output.ssi_pid, state.info.ssi_pid);
  EXPECT_EQ(output.ssi_int, state.info.ssi_int);
  EXPECT_EQ(output.ssi_overrun, state.info.ssi_overrun);
  auto notification = SENTINEL;
  EXPECT_FALSE(transport::decode_timer(&output, sizeof(output), SIGNAL,
                                       TIMER_ID + 1, notification));
  EXPECT_EQ(notification.token, SENTINEL.token);
  ASSERT_TRUE(transport::decode_timer(&output, sizeof(output), SIGNAL, TIMER_ID,
                                      notification));
  EXPECT_EQ(notification.token, TOKEN);
  EXPECT_TRUE(notification.event == transport::Event::TimerExpired);
  EXPECT_EQ(notification.overrun, uint32_t(7));

  // Intake does not pretend to validate an unknown token against the registry.
  state.info.ssi_signo = SIGNAL + 1;
  state.info.ssi_code = SI_USER;
  state.info.ssi_tid = UINT32_MAX;
  state.info.ssi_ptr = 0;
  ASSERT_TRUE(channels.read_timer(output).has_value());
  EXPECT_EQ(output.ssi_tid, __u32(UINT32_MAX));
  EXPECT_EQ(output.ssi_code, static_cast<int>(SI_USER));
  EXPECT_EQ(output.ssi_signo, __u32(SIGNAL + 1));
  EXPECT_EQ(output.ssi_ptr, __u64(0));
}

TEST(LlvmLibcNotificationChannelsTest, MqTerminalRecordsAndOneAttemptErrors) {
  State state;
  Channels channels(FakeOperations{&state});
  ASSERT_TRUE(channels.initialize(SIGNAL).has_value());
  ASSERT_TRUE(channels.ensure_mq().has_value());
  const unsigned char statuses[] = {transport::MQ_WOKENUP,
                                    transport::MQ_REMOVED};
  for (unsigned char status : statuses) {
    state.cookie[31] = status;
    auto output = SENTINEL;
    int calls = state.recvmsg_calls;
    ASSERT_TRUE(channels.read_mq(output).has_value());
    EXPECT_EQ(state.recvmsg_calls, calls + 1);
    EXPECT_EQ(output.token, TOKEN);
    EXPECT_TRUE(output.event == (status == transport::MQ_WOKENUP
                                     ? transport::Event::MqWokenUp
                                     : transport::Event::MqRemoved));
    EXPECT_EQ(output.overrun, uint32_t(0));
  }
  const int errors[] = {EINTR, EAGAIN, ENOBUFS, EIO, EBADF};
  for (int error : errors) {
    state.recvmsg_error = error;
    reject_mq(channels, state, error);
  }
}

TEST(LlvmLibcNotificationChannelsTest, MqPassesLengthsAndTruncationToParser) {
  State state;
  Channels channels(FakeOperations{&state});
  ASSERT_TRUE(channels.initialize(SIGNAL).has_value());
  ASSERT_TRUE(channels.ensure_mq().has_value());
  for (size_t size = 0; size < 32; ++size) {
    state.mq_size = size;
    reject_mq(channels, state, EPROTO);
  }
  const size_t oversized[] = {33, 64, SIZE_MAX};
  for (size_t size : oversized) {
    state.mq_size = size;
    reject_mq(channels, state, EPROTO);
  }
  state.mq_size = 32;
  state.message_flags = MSG_TRUNC;
  reject_mq(channels, state, EPROTO);
  state.message_flags = MSG_TRUNC | MSG_CTRUNC;
  reject_mq(channels, state, EPROTO);
  state.message_flags = 0;
  for (size_t size = 0; size <= sizeof(sockaddr_nl) + 1; ++size) {
    if (size == sizeof(sockaddr_nl))
      continue;
    state.source_size = size;
    reject_mq(channels, state, EPROTO);
  }
}

TEST(LlvmLibcNotificationChannelsTest, MqPassesSourceAndCookieToParser) {
  State state;
  Channels channels(FakeOperations{&state});
  ASSERT_TRUE(channels.initialize(SIGNAL).has_value());
  ASSERT_TRUE(channels.ensure_mq().has_value());
  state.source.nl_family = AF_UNIX;
  reject_mq(channels, state, EPROTO);
  state.source.nl_family = AF_NETLINK;
  state.source.nl_pad = 1;
  reject_mq(channels, state, EPROTO);
  state.source.nl_pad = 0;
  state.source.nl_pid = 12345;
  reject_mq(channels, state, EPROTO);
  state.source.nl_pid = 0;
  state.source.nl_groups = 1;
  reject_mq(channels, state, EPROTO);
  state.source.nl_groups = 0;
  state.cookie[31] = 0;
  reject_mq(channels, state, EPROTO);
  state.cookie[31] = transport::MQ_WOKENUP;
  state.cookie[8] = 1;
  reject_mq(channels, state, EPROTO);
  state.cookie[8] = 0;
  for (size_t i = 0; i < 8; ++i)
    state.cookie[i] = 0;
  reject_mq(channels, state, EPROTO);
}

TEST(LlvmLibcNotificationChannelsTest, PositiveErrorsDoNotChangeLibcErrno) {
  State state;
  Channels channels(FakeOperations{&state});
  int saved_errno = libc_errno;
  libc_errno = EDOM;
  state.signalfd_error = ENOSYS;
  state.close_errors[0] = EINTR;
  expect_error(channels.initialize(SIGNAL), ENOSYS);
  EXPECT_EQ(int(libc_errno), EDOM);
  state.signalfd_error = 0;
  EXPECT_TRUE(channels.initialize(SIGNAL).has_value());
  state.socket_error = EMFILE;
  expect_error(channels.ensure_mq(), EMFILE);
  state.socket_error = 0;
  EXPECT_TRUE(channels.ensure_mq().has_value());
  state.write_error = EINTR;
  expect_error(channels.wake(), EINTR);
  state.read_error = EAGAIN;
  reject_control(channels, state, EAGAIN);
  reject_timer(channels, state, EAGAIN);
  state.recvmsg_error = ENOBUFS;
  reject_mq(channels, state, ENOBUFS);
  expect_error(channels.reset(), EINTR);
  EXPECT_EQ(int(libc_errno), EDOM);
  libc_errno = saved_errno;
}
