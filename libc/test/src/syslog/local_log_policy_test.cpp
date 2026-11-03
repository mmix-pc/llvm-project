//===-- Local syslog policy boundary regressions --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Standalone provider-inclusion test, like local_log_test.cpp. No host logging
// service is used. Enable C++ exceptions to include the unwind injection cases;
// those cases test resource ownership, not target cancellation delivery.
#include "hdr/func/free.h"
#include "src/stdio/asprintf.h"
#include "src/stdio/vasprintf.h"
#include "src/time/localtime_r.h"
#include "src/time/time_func.h"
#include "test/UnitTest/Test.h"

void policy_free(void *);
namespace LIBC_NAMESPACE_DECL {
int policy_vasprintf(char **, const char *, va_list);
int policy_asprintf(char **, const char *, ...);
}
#define free policy_free
#define vasprintf policy_vasprintf
#define asprintf policy_asprintf
#include "src/syslog/linux/log.cpp"
#undef asprintf
#undef vasprintf
#undef free
#include "src/syslog/closelog.cpp"
#include "src/syslog/openlog.cpp"
#include "src/syslog/setlogmask.cpp"
#include "src/syslog/syslog.cpp"

namespace {
int sockets, connects, sends, closes, consoles, writes, live;
int send_errors[2], connect_error, socket_error, write_error, console_error;
int format_failure, clock_failure, throw_at;
int month, day, hour;
char packet[8192];
size_t packet_size;

void unwind_at(int point) {
#ifdef __EXCEPTIONS
  if (throw_at == point)
    throw point;
#else
  (void)point;
#endif
}
} // namespace

void policy_free(void *ptr) {
  if (ptr)
    --live;
  ::free(ptr);
}

namespace LIBC_NAMESPACE_DECL {
int policy_vasprintf(char **out, const char *format, va_list args) {
  if (format_failure == 1) {
    libc_errno = ENOMEM;
    return -1;
  }
  int result = LIBC_NAMESPACE::vasprintf(out, format, args);
  if (*out)
    ++live;
  return result;
}
int policy_asprintf(char **out, const char *format, ...) {
  unwind_at(1); // Body is owned before message formatting starts.
  if (format_failure == 2) {
    libc_errno = ENOMEM;
    return -1;
  }
  va_list args;
  va_start(args, format);
  int result = LIBC_NAMESPACE::vasprintf(out, format, args);
  va_end(args);
  if (*out)
    ++live;
  return result;
}
time_t time(time_t *out) {
  EXPECT_TRUE(out == nullptr);
  libc_errno = EIO; // Setup must not contaminate the caller's %m.
  return clock_failure == 1 ? time_t(-1) : time_t(123456);
}
struct tm *localtime_r(const time_t *now, struct tm *out) {
  EXPECT_EQ(*now, time_t(123456));
  libc_errno = EOVERFLOW;
  if (clock_failure == 2)
    return nullptr;
  *out = {};
  out->tm_mon = month;
  out->tm_mday = day;
  out->tm_hour = hour;
  out->tm_min = 4;
  out->tm_sec = 5;
  return out;
}
int socket(int domain, int type, int protocol) {
  ++sockets;
  EXPECT_EQ(domain, AF_UNIX);
  EXPECT_EQ(type, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK);
  EXPECT_EQ(protocol, 0);
  if (socket_error) {
    libc_errno = socket_error;
    return -1;
  }
  return 42;
}
int connect(int fd, const sockaddr *address, socklen_t length) {
  ++connects;
  EXPECT_EQ(fd, 42);
  EXPECT_EQ(length, socklen_t(offsetof(sockaddr_un, sun_path) + 9));
  auto *un = reinterpret_cast<const sockaddr_un *>(address);
  EXPECT_EQ(un->sun_family, static_cast<sa_family_t>(AF_UNIX));
  EXPECT_STREQ(un->sun_path, "/dev/log");
  unwind_at(2);
  if (connect_error) {
    libc_errno = connect_error;
    return -1;
  }
  return 0;
}
ssize_t send(int fd, const void *data, size_t size, int flags) {
  EXPECT_EQ(fd, 42);
  EXPECT_EQ(flags, int(MSG_NOSIGNAL));
  int index = sends++;
  unwind_at(3);
  if (index < 2 && send_errors[index]) {
    libc_errno = send_errors[index];
    return -1;
  }
  EXPECT_LT(size, sizeof(packet));
  if (size >= sizeof(packet))
    return -1;
  auto *bytes = static_cast<const char *>(data);
  for (size_t i = 0; i < size; ++i)
    packet[i] = bytes[i];
  packet[size] = '\0';
  packet_size = size;
  return static_cast<ssize_t>(size);
}
int close(int fd) {
  EXPECT_TRUE(fd == 42 || fd == 43);
  ++closes;
  libc_errno = EBADF;
  return 0;
}
int open(const char *path, int flags, ...) {
  ++consoles;
  EXPECT_STREQ(path, "/dev/console");
  EXPECT_EQ(flags, O_WRONLY | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
  if (console_error) {
    libc_errno = console_error;
    return -1;
  }
  return 43;
}
ssize_t write(int fd, const void *, size_t size) {
  EXPECT_TRUE(fd == 43 || fd == 2);
  ++writes;
  unwind_at(4);
  if (write_error) {
    libc_errno = write_error;
    return -1;
  }
  return static_cast<ssize_t>(size > 3 ? 3 : size);
}
pid_t getpid() { return 123; }
} // namespace LIBC_NAMESPACE_DECL

class LlvmLibcLocalLogPolicyTest : public LIBC_NAMESPACE::testing::Test {
protected:
  void SetUp() override {
    LIBC_NAMESPACE::closelog();
    LIBC_NAMESPACE::openlog("app", 0, LOG_USER);
    LIBC_NAMESPACE::setlogmask(255);
    sockets = connects = sends = closes = consoles = writes = live = 0;
    send_errors[0] = send_errors[1] = 0;
    connect_error = socket_error = write_error = console_error = 0;
    format_failure = clock_failure = throw_at = 0;
    month = 10;
    day = 3;
    hour = 12;
    packet_size = 0;
    packet[0] = '\0';
    LIBC_NAMESPACE::libc_errno = ENOENT;
  }
  void TearDown() override {
    EXPECT_EQ(live, 0);
    LIBC_NAMESPACE::closelog();
  }
};

// Timestamp, body and borrowed-ident policy.
TEST_F(LlvmLibcLocalLogPolicyTest, TimestampAndEntryErrno) {
  LIBC_NAMESPACE::syslog(LOG_INFO, "%m");
  EXPECT_STREQ(packet, "<14>Nov  3 12:04:05 app: No such file or directory");
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOENT);
  EXPECT_NE(packet[packet_size - 1], '\0');
  EXPECT_EQ(sends, 1);
  EXPECT_EQ(live, 0);
}

TEST_F(LlvmLibcLocalLogPolicyTest, EnglishMonthsAndLocalFields) {
  const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  for (month = 0; month < 12; ++month) {
    day = 23;
    hour = 7;
    LIBC_NAMESPACE::syslog(LOG_INFO, "body");
    for (int i = 0; i < 3; ++i)
      EXPECT_EQ(packet[4 + i], months[month][i]);
    EXPECT_STREQ(packet + 7, " 23 07:04:05 app: body");
  }
}

TEST_F(LlvmLibcLocalLogPolicyTest, IdentPidAndBinaryBody) {
  char ident[] = "borrowed";
  LIBC_NAMESPACE::openlog(ident, LOG_PID, LOG_LOCAL0);
  EXPECT_TRUE(LIBC_NAMESPACE::local_log::tag == ident);
  LIBC_NAMESPACE::openlog(nullptr, LOG_PID, LOG_LOCAL0);
  LIBC_NAMESPACE::syslog(LOG_ERR, "a%cb\n", 0);
  EXPECT_STREQ(packet, "<131>Nov  3 12:04:05 borrowed[123]: a");
  EXPECT_EQ(packet[packet_size - 3], '\0');
  EXPECT_EQ(packet[packet_size - 2], 'b');
  EXPECT_EQ(packet[packet_size - 1], '\n');
  LIBC_NAMESPACE::closelog();
  EXPECT_TRUE(LIBC_NAMESPACE::local_log::tag == nullptr);
  LIBC_NAMESPACE::openlog("", 0, LOG_USER);
  LIBC_NAMESPACE::syslog(LOG_INFO, "empty");
  EXPECT_STREQ(packet, "<14>Nov  3 12:04:05 : empty");
}

TEST_F(LlvmLibcLocalLogPolicyTest, PreparationFailures) {
  for (int failure = 1; failure <= 4; ++failure) {
    clock_failure = failure <= 2 ? failure : 0;
    format_failure = failure > 2 ? failure - 2 : 0;
    LIBC_NAMESPACE::syslog(LOG_INFO, "failure");
    EXPECT_EQ(sockets, 0);
    EXPECT_EQ(writes, 0);
    EXPECT_EQ(live, 0);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOENT);
  }
}

// Bounded transport, independent of receiver availability.
TEST_F(LlvmLibcLocalLogPolicyTest, NoRetryForNonStaleErrors) {
  const int errors[] = {EINTR, EAGAIN, EMSGSIZE, EACCES, ENOBUFS, ENOMEM};
  for (int error : errors) {
    sends = 0;
    send_errors[0] = error;
    LIBC_NAMESPACE::syslog(LOG_INFO, "lost");
    EXPECT_EQ(sends, 1);
    EXPECT_EQ(sockets, 1);
    EXPECT_EQ(closes, 0);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOENT);
  }
}

TEST_F(LlvmLibcLocalLogPolicyTest, SingleStaleReconnect) {
  const int errors[] = {ECONNREFUSED, ECONNRESET, ENOTCONN};
  for (int error : errors) {
    sends = sockets = closes = 0;
    send_errors[0] = send_errors[1] = error;
    LIBC_NAMESPACE::syslog(LOG_INFO, "lost");
    EXPECT_EQ(sends, 2);
    EXPECT_EQ(sockets, 2);
    EXPECT_EQ(closes, 2);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOENT);
  }
  sends = 0;
  send_errors[1] = 0;
  LIBC_NAMESPACE::syslog(LOG_INFO, "recovered");
  EXPECT_EQ(sends, 2);
  EXPECT_STREQ(packet, "<14>Nov  3 12:04:05 app: recovered");
}

TEST_F(LlvmLibcLocalLogPolicyTest, InitialConnectionFailureIsNotRetried) {
  connect_error = ENOENT;
  LIBC_NAMESPACE::syslog(LOG_INFO, "absent");
  EXPECT_EQ(sockets, 1);
  EXPECT_EQ(connects, 1);
  EXPECT_EQ(closes, 1);
  EXPECT_EQ(sends, 0);
  socket_error = EMFILE;
  LIBC_NAMESPACE::syslog(LOG_INFO, "no descriptors");
  EXPECT_EQ(sockets, 2);
  EXPECT_EQ(connects, 1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOENT);
}

TEST_F(LlvmLibcLocalLogPolicyTest, ConsoleIsBestEffort) {
  LIBC_NAMESPACE::openlog("app", LOG_CONS, LOG_USER);
  send_errors[0] = EAGAIN;
  write_error = EINTR;
  LIBC_NAMESPACE::syslog(LOG_INFO, "lost");
  EXPECT_EQ(sends, 1);
  EXPECT_EQ(consoles, 1);
  EXPECT_EQ(writes, 2); // One text attempt and one CRLF attempt, no retries.
  EXPECT_EQ(closes, 1);
  EXPECT_EQ(live, 0);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOENT);
}

TEST_F(LlvmLibcLocalLogPolicyTest, UnavailableConsoleStops) {
  LIBC_NAMESPACE::openlog("app", LOG_CONS, LOG_USER);
  send_errors[0] = EAGAIN;
  console_error = EACCES;
  LIBC_NAMESPACE::syslog(LOG_INFO, "lost");
  EXPECT_EQ(sends, 1);
  EXPECT_EQ(consoles, 1);
  EXPECT_EQ(writes, 0);
  EXPECT_EQ(live, 0);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOENT);
}

TEST_F(LlvmLibcLocalLogPolicyTest, ForkSnapshotBusyAndParentRollback) {
  LIBC_NAMESPACE::openlog("parent", LOG_NDELAY | LOG_PID, LOG_LOCAL0);
  ASSERT_TRUE(LIBC_NAMESPACE::internal::try_lock_log_for_fork());
  EXPECT_FALSE(LIBC_NAMESPACE::internal::try_lock_log_for_fork());
  LIBC_NAMESPACE::internal::finish_log_fork(false);
  EXPECT_EQ(closes, 0);
  LIBC_NAMESPACE::syslog(LOG_INFO, "retained");
  EXPECT_EQ(sockets, 1);
  EXPECT_STREQ(packet, "<134>Nov  3 12:04:05 parent[123]: retained");
}

TEST_F(LlvmLibcLocalLogPolicyTest, ChildSnapshotDropsOnlyConnection) {
  // A real disposable descriptor tests the raw, non-cancellable child close.
  long fd = LIBC_NAMESPACE::syscall_impl<long>(SYS_dup, 2);
  ASSERT_GE(fd, 0L);
  LIBC_NAMESPACE::openlog("child", LOG_PID, LOG_LOCAL0);
  LIBC_NAMESPACE::setlogmask(LOG_MASK(LOG_INFO));
  LIBC_NAMESPACE::local_log::descriptor = static_cast<int>(fd);
  ASSERT_TRUE(LIBC_NAMESPACE::internal::try_lock_log_for_fork());
  LIBC_NAMESPACE::internal::finish_log_fork(true);
  EXPECT_EQ(LIBC_NAMESPACE::syscall_impl<long>(SYS_fcntl, fd, F_GETFD),
            -long(EBADF));
  EXPECT_EQ(LIBC_NAMESPACE::local_log::descriptor, -1);
  EXPECT_EQ(closes, 0);
  LIBC_NAMESPACE::syslog(LOG_ERR, "filtered");
  EXPECT_EQ(sockets, 0);
  LIBC_NAMESPACE::syslog(LOG_INFO, "reconnected");
  EXPECT_EQ(sockets, 1);
  EXPECT_STREQ(packet, "<134>Nov  3 12:04:05 child[123]: reconnected");
}

// Resource cleanup and lock release on stack unwinding.
#ifdef __EXCEPTIONS
TEST_F(LlvmLibcLocalLogPolicyTest, UnwindReleasesResourcesAndLock) {
  for (int point = 1; point <= 4; ++point) {
    SetUp();
    throw_at = point;
    if (point == 4) {
      LIBC_NAMESPACE::openlog("app", LOG_CONS, LOG_USER);
      send_errors[0] = EAGAIN;
    }
    bool caught = false;
    try {
      LIBC_NAMESPACE::syslog(LOG_INFO, "unwind");
    } catch (int value) {
      caught = true;
      EXPECT_EQ(value, point);
    }
    EXPECT_TRUE(caught);
    EXPECT_EQ(live, 0);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOENT);
    EXPECT_EQ(closes, point == 2 || point == 4 ? 1 : 0);
    ASSERT_EQ(LIBC_NAMESPACE::local_log::mutex.try_lock(),
              LIBC_NAMESPACE::MutexError::NONE);
    LIBC_NAMESPACE::local_log::mutex.unlock();
    throw_at = 0;
  }
}

TEST_F(LlvmLibcLocalLogPolicyTest, UnwindAfterDeliveryDoesNotResend) {
  LIBC_NAMESPACE::openlog("app", LOG_PERROR | LOG_CONS, LOG_USER);
  throw_at = 4;
  bool caught = false;
  try {
    LIBC_NAMESPACE::syslog(LOG_INFO, "delivered");
  } catch (int point) {
    caught = true;
    EXPECT_EQ(point, 4);
  }
  EXPECT_TRUE(caught);
  EXPECT_EQ(sends, 1);
  EXPECT_EQ(consoles, 0);
  EXPECT_EQ(live, 0);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOENT);
  ASSERT_EQ(LIBC_NAMESPACE::local_log::mutex.try_lock(),
            LIBC_NAMESPACE::MutexError::NONE);
  LIBC_NAMESPACE::local_log::mutex.unlock();
}
#endif
