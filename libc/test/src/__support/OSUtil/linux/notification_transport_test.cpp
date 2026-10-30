//===-- Tests for Linux notification transport ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/linux/notification_transport.h"
#include "test/UnitTest/Test.h"
#include <linux/mqueue.h>

namespace transport = LIBC_NAMESPACE::notification_transport;
using transport::Event;
using transport::Notification;

static_assert(transport::MQ_COOKIE_SIZE == NOTIFY_COOKIE_LEN);
static_assert(transport::MQ_WOKENUP == NOTIFY_WOKENUP);
static_assert(transport::MQ_REMOVED == NOTIFY_REMOVED);

namespace {
constexpr uint64_t TOKEN = UINT64_C(0x8123456789abcdef);
constexpr int SIGNAL = 34;
constexpr int TIMER_ID = 73;
constexpr Notification SENTINEL = {42, Event::MqRemoved, 19};
constexpr uint64_t TOKENS[] = {1, UINT64_C(0x100000000), TOKEN,
                               UINT64_C(0x8000000000000000), UINT64_MAX};

sockaddr_nl kernel_source() {
  sockaddr_nl source{};
  source.nl_family = AF_NETLINK;
  return source;
}

signalfd_siginfo timer_record(uint64_t token = TOKEN, int id = TIMER_ID) {
  signalfd_siginfo info{};
  info.ssi_signo = SIGNAL;
  info.ssi_code = SI_TIMER;
  info.ssi_tid = static_cast<uint32_t>(id);
  info.ssi_ptr = token;
  info.ssi_overrun = 7;
  return info;
}

void expect_unchanged(const Notification &output) {
  EXPECT_EQ(output.token, SENTINEL.token);
  EXPECT_TRUE(output.event == SENTINEL.event);
  EXPECT_EQ(output.overrun, SENTINEL.overrun);
}

void reject_mq(const unsigned char *cookie, size_t length,
               const sockaddr_nl *source, size_t source_length,
               int message_flags = 0) {
  Notification output = SENTINEL;
  EXPECT_FALSE(transport::decode_mq(cookie, length, message_flags, source,
                                    source_length, output));
  expect_unchanged(output);
}

void reject_timer(const signalfd_siginfo *info, size_t length,
                  int signal = SIGNAL, int id = TIMER_ID) {
  Notification output = SENTINEL;
  EXPECT_FALSE(transport::decode_timer(info, length, signal, id, output));
  expect_unchanged(output);
}
} // namespace

TEST(LlvmLibcNotificationTransportTest, MqFixedByteOrderAndZeroFill) {
  unsigned char cookie[transport::MQ_COOKIE_SIZE];
  for (unsigned char &byte : cookie)
    byte = 0xa5;
  ASSERT_TRUE(transport::encode_mq_cookie(TOKEN, cookie));
  const unsigned char expected[] = {0x81, 0x23, 0x45, 0x67,
                                    0x89, 0xab, 0xcd, 0xef};
  for (size_t i = 0; i < sizeof(expected); ++i)
    EXPECT_EQ(cookie[i], expected[i]);
  for (size_t i = sizeof(expected); i < sizeof(cookie); ++i)
    EXPECT_EQ(cookie[i], static_cast<unsigned char>(0));

  for (unsigned char &byte : cookie)
    byte = 0xa5;
  EXPECT_FALSE(transport::encode_mq_cookie(0, cookie));
  for (unsigned char byte : cookie)
    EXPECT_EQ(byte, static_cast<unsigned char>(0xa5));
}

TEST(LlvmLibcNotificationTransportTest, MqTerminalEventsAndHighTokens) {
  const sockaddr_nl source = kernel_source();
  const unsigned char statuses[] = {NOTIFY_WOKENUP, NOTIFY_REMOVED};
  for (uint64_t token : TOKENS) {
    for (unsigned char status : statuses) {
      unsigned char cookie[transport::MQ_COOKIE_SIZE];
      ASSERT_TRUE(transport::encode_mq_cookie(token, cookie));
      cookie[sizeof(cookie) - 1] = status;
      Notification output = SENTINEL;
      ASSERT_TRUE(transport::decode_mq(cookie, sizeof(cookie), 0, &source,
                                       sizeof(source), output));
      EXPECT_EQ(output.token, token);
      EXPECT_TRUE(
          output.event ==
          (status == NOTIFY_WOKENUP ? Event::MqWokenUp : Event::MqRemoved));
      EXPECT_EQ(output.overrun, uint32_t(0));
    }
  }
}

TEST(LlvmLibcNotificationTransportTest, MqIndependentWireFixture) {
  // A raw, potentially unaligned payload with no nlmsghdr or host-endian token.
  unsigned char storage[33] = {0xa5, 0xfe, 0xdc, 0xba, 0x98,
                               0x76, 0x54, 0x32, 0x10};
  storage[32] = NOTIFY_REMOVED;
  const sockaddr_nl source = kernel_source();
  Notification output = SENTINEL;
  ASSERT_TRUE(transport::decode_mq(storage + 1, 32, 0, &source, sizeof(source),
                                   output));
  EXPECT_EQ(output.token, UINT64_C(0xfedcba9876543210));
  EXPECT_TRUE(output.event == Event::MqRemoved);
}

TEST(LlvmLibcNotificationTransportTest, MqLengthAndTruncation) {
  unsigned char cookie[transport::MQ_COOKIE_SIZE];
  ASSERT_TRUE(transport::encode_mq_cookie(TOKEN, cookie));
  cookie[sizeof(cookie) - 1] = NOTIFY_WOKENUP;
  const sockaddr_nl source = kernel_source();
  for (size_t length = 0; length < sizeof(cookie); ++length)
    reject_mq(cookie, length, &source, sizeof(source));
  reject_mq(cookie, sizeof(cookie) + 1, &source, sizeof(source));
  reject_mq(cookie, 2 * sizeof(cookie), &source, sizeof(source));
  reject_mq(cookie, size_t(-1), &source, sizeof(source));
  reject_mq(nullptr, sizeof(cookie), &source, sizeof(source));
  reject_mq(cookie, sizeof(cookie), &source, sizeof(source), MSG_TRUNC);
  reject_mq(cookie, sizeof(cookie), &source, sizeof(source),
            MSG_TRUNC | MSG_CTRUNC);
}

TEST(LlvmLibcNotificationTransportTest, MqKernelSourceValidation) {
  unsigned char cookie[transport::MQ_COOKIE_SIZE];
  ASSERT_TRUE(transport::encode_mq_cookie(TOKEN, cookie));
  cookie[sizeof(cookie) - 1] = NOTIFY_WOKENUP;
  sockaddr_nl source = kernel_source();
  reject_mq(cookie, sizeof(cookie), nullptr, sizeof(source));
  for (size_t length = 0; length < sizeof(source); ++length)
    reject_mq(cookie, sizeof(cookie), &source, length);
  reject_mq(cookie, sizeof(cookie), &source, sizeof(source) + 1);
  reject_mq(cookie, sizeof(cookie), &source, size_t(-1));
  source.nl_family = AF_UNIX;
  reject_mq(cookie, sizeof(cookie), &source, sizeof(source));
  source = kernel_source();
  source.nl_pad = 1;
  reject_mq(cookie, sizeof(cookie), &source, sizeof(source));
  source = kernel_source();
  const uint32_t nonzero[] = {1, 12345, UINT32_C(0x80000000), UINT32_MAX};
  for (uint32_t value : nonzero) {
    source.nl_pid = value;
    reject_mq(cookie, sizeof(cookie), &source, sizeof(source));
    source.nl_pid = 0;
    source.nl_groups = value;
    reject_mq(cookie, sizeof(cookie), &source, sizeof(source));
    source.nl_groups = 0;
  }
}

TEST(LlvmLibcNotificationTransportTest, MqMalformedStatusPaddingAndZeroToken) {
  unsigned char cookie[transport::MQ_COOKIE_SIZE];
  ASSERT_TRUE(transport::encode_mq_cookie(TOKEN, cookie));
  const sockaddr_nl source = kernel_source();
  for (unsigned status = 0; status <= 255; ++status) {
    if (status == NOTIFY_WOKENUP || status == NOTIFY_REMOVED)
      continue;
    cookie[sizeof(cookie) - 1] = static_cast<unsigned char>(status);
    reject_mq(cookie, sizeof(cookie), &source, sizeof(source));
  }
  cookie[sizeof(cookie) - 1] = NOTIFY_WOKENUP;
  for (size_t i = 8; i < sizeof(cookie) - 1; ++i) {
    cookie[i] = 0x80;
    reject_mq(cookie, sizeof(cookie), &source, sizeof(source));
    cookie[i] = 0;
  }
  for (size_t i = 0; i < 8; ++i)
    cookie[i] = 0;
  reject_mq(cookie, sizeof(cookie), &source, sizeof(source));
  cookie[sizeof(cookie) - 1] = NOTIFY_REMOVED;
  reject_mq(cookie, sizeof(cookie), &source, sizeof(source));
}

TEST(LlvmLibcNotificationTransportTest, TimerUapiAndHighTokens) {
  for (uint64_t token : TOKENS) {
    auto info = timer_record(token);
    // Neither ssi_int nor ssi_pid is the token or timer ID in signalfd's ABI.
    info.ssi_int = -1;
    info.ssi_pid = 9876;
    Notification output = SENTINEL;
    ASSERT_TRUE(
        transport::decode_timer(&info, sizeof(info), SIGNAL, TIMER_ID, output));
    EXPECT_EQ(output.token, token);
    EXPECT_TRUE(output.event == Event::TimerExpired);
    EXPECT_EQ(output.overrun, uint32_t(7));
  }
}

TEST(LlvmLibcNotificationTransportTest, TimerExactRecordLength) {
  const auto info = timer_record();
  for (size_t length = 0; length < sizeof(info); ++length)
    reject_timer(&info, length);
  reject_timer(&info, sizeof(info) + 1);
  reject_timer(&info, 2 * sizeof(info));
  reject_timer(&info, size_t(-1));
  reject_timer(nullptr, sizeof(info));
}

TEST(LlvmLibcNotificationTransportTest, TimerOverrunIsPreserved) {
  const uint32_t overruns[] = {0, 1, INT32_MAX};
  for (uint32_t overrun : overruns) {
    auto info = timer_record();
    info.ssi_overrun = overrun;
    Notification output = SENTINEL;
    ASSERT_TRUE(
        transport::decode_timer(&info, sizeof(info), SIGNAL, TIMER_ID, output));
    EXPECT_EQ(output.overrun, overrun);
  }
}

TEST(LlvmLibcNotificationTransportTest, TimerSignalAndOrigin) {
  auto info = timer_record();
  const uint32_t wrong_signals[] = {0, 32, 33, 35, UINT32_MAX};
  for (uint32_t signal : wrong_signals) {
    info.ssi_signo = signal;
    reject_timer(&info, sizeof(info));
  }
  info = timer_record();
  reject_timer(&info, sizeof(info), 0);
  reject_timer(&info, sizeof(info), -1);
  reject_timer(&info, sizeof(info), SIGNAL + 1);
  const int wrong_codes[] = {SI_USER, SI_QUEUE, SI_TKILL, SI_KERNEL, 12345};
  for (int code : wrong_codes) {
    info.ssi_code = code;
    reject_timer(&info, sizeof(info));
  }

  // Signal reservation belongs to the caller, not a hard-coded transport ABI.
  info = timer_record();
  info.ssi_signo = SIGNAL + 1;
  Notification output = SENTINEL;
  ASSERT_TRUE(transport::decode_timer(&info, sizeof(info), SIGNAL + 1, TIMER_ID,
                                      output));
  EXPECT_EQ(output.token, TOKEN);
}

TEST(LlvmLibcNotificationTransportTest, TimerCookieAndKernelIdentity) {
  auto info = timer_record(0);
  info.ssi_int = 1;
  reject_timer(&info, sizeof(info));
  info = timer_record();
  reject_timer(&info, sizeof(info), SIGNAL, TIMER_ID + 1);
  reject_timer(&info, sizeof(info), SIGNAL, -1);
  const uint32_t wrong_ids[] = {0, TIMER_ID + 1, UINT32_C(0x80000000),
                                UINT32_MAX};
  for (uint32_t id : wrong_ids) {
    info.ssi_tid = id;
    info.ssi_pid = TIMER_ID;
    reject_timer(&info, sizeof(info));
  }
  info.ssi_tid = UINT32_MAX;
  reject_timer(&info, sizeof(info), SIGNAL, -1);

  // Linux IDs include zero; no conversion through public timer_t is needed.
  const int valid_ids[] = {0, INT32_MAX};
  for (int id : valid_ids) {
    info = timer_record(TOKEN, id);
    info.ssi_overrun = 0;
    Notification output = SENTINEL;
    ASSERT_TRUE(
        transport::decode_timer(&info, sizeof(info), SIGNAL, id, output));
    EXPECT_EQ(output.token, TOKEN);
    EXPECT_EQ(output.overrun, uint32_t(0));
  }
}
