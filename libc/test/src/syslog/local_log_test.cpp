//===-- Tests for local syslog formatting and transport -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Intercept every transport boundary: never contact the host's log or console.
#include "src/stdio/asprintf.h"
#include "src/stdio/vasprintf.h"
namespace LIBC_NAMESPACE_DECL {
int checked_vasprintf(char **out, const char *format, va_list args);
int checked_asprintf(char **out, const char *format, ...);
} // namespace LIBC_NAMESPACE_DECL
#define vasprintf checked_vasprintf
#define asprintf checked_asprintf
#include "src/syslog/linux/log.cpp"
#undef asprintf
#undef vasprintf
#include "src/syslog/closelog.cpp"
#include "src/syslog/openlog.cpp"
#include "src/syslog/setlogmask.cpp"
#include "src/syslog/syslog.cpp"
#include "src/syslog/vsyslog.cpp"
#include "test/UnitTest/Test.h"

namespace {
int sockets, connects, sends, closes, consoles, writes;
int socket_error, connect_error, send_error;
int format_failure;
bool interrupt_send;
char datagram[8192];
char fallback[8192];
size_t fallback_size;
} // namespace

namespace LIBC_NAMESPACE_DECL {
int checked_vasprintf(char **out, const char *format, va_list args) {
  if (format_failure == 1) {
    *out = nullptr;
    libc_errno = ENOMEM;
    return -1;
  }
  return LIBC_NAMESPACE::vasprintf(out, format, args);
}
int checked_asprintf(char **out, const char *format, ...) {
  if (format_failure == 2) {
    *out = nullptr;
    libc_errno = ENOMEM;
    return -1;
  }
  va_list args;
  va_start(args, format);
  int result = LIBC_NAMESPACE::vasprintf(out, format, args);
  va_end(args);
  return result;
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
  if (connect_error) {
    libc_errno = connect_error;
    return -1;
  }
  return 0;
}
ssize_t send(int fd, const void *buffer, size_t size, int flags) {
  ++sends;
  EXPECT_EQ(fd, 42);
  EXPECT_EQ(flags, MSG_NOSIGNAL);
  if (interrupt_send) {
    interrupt_send = false;
    libc_errno = EINTR;
    return -1;
  }
  if (send_error) {
    libc_errno = send_error;
    return -1;
  }
  EXPECT_LT(size, sizeof(datagram));
  if (size >= sizeof(datagram))
    return -1;
  auto *bytes = static_cast<const char *>(buffer);
  EXPECT_NE(bytes[size - 1], '\0');
  for (size_t i = 0; i < size; ++i)
    datagram[i] = bytes[i];
  datagram[size] = '\0';
  return static_cast<ssize_t>(size);
}
int close(int fd) {
  EXPECT_TRUE(fd == 42 || fd == 43);
  ++closes;
  return 0;
}
int open(const char *path, int flags, ...) {
  EXPECT_STREQ(path, "/dev/console");
  EXPECT_EQ(flags, O_WRONLY | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
  ++consoles;
  return 43;
}
ssize_t write(int fd, const void *buffer, size_t size) {
  EXPECT_TRUE(fd == 2 || fd == 43);
  ++writes;
  auto *bytes = static_cast<const char *>(buffer);
  // Force partial writes so the fallback loop is exercised.
  if (size > 3)
    size = 3;
  for (size_t i = 0; i < size && fallback_size + 1 < sizeof(fallback); ++i)
    fallback[fallback_size++] = bytes[i];
  fallback[fallback_size] = '\0';
  return static_cast<ssize_t>(size);
}
pid_t getpid() { return 123; }
} // namespace LIBC_NAMESPACE_DECL

class LlvmLibcLocalLogTest : public LIBC_NAMESPACE::testing::Test {
public:
  void SetUp() override {
    LIBC_NAMESPACE::closelog();
    LIBC_NAMESPACE::openlog("test", 0, LOG_USER);
    LIBC_NAMESPACE::setlogmask(0xff);
    sockets = connects = sends = closes = consoles = writes = 0;
    socket_error = connect_error = send_error = 0;
    format_failure = 0;
    interrupt_send = false;
    datagram[0] = fallback[0] = '\0';
    fallback_size = 0;
  }
};

TEST_F(LlvmLibcLocalLogTest, FormatMaskAndLifecycle) {
  LIBC_NAMESPACE::openlog("test", LOG_PID | LOG_NDELAY, LOG_LOCAL0);
  EXPECT_EQ(sockets, 1);
  LIBC_NAMESPACE::libc_errno = EDOM;
  LIBC_NAMESPACE::syslog(LOG_ERR, "value=%d", 7);
  EXPECT_STREQ(datagram, "<131>test[123]: value=7");
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  EXPECT_EQ(LIBC_NAMESPACE::setlogmask(LOG_MASK(LOG_ERR)), 255);
  EXPECT_EQ(LIBC_NAMESPACE::setlogmask(0), int(LOG_MASK(LOG_ERR)));
  LIBC_NAMESPACE::syslog(LOG_INFO, "filtered");
  LIBC_NAMESPACE::syslog(-1, "invalid");
  LIBC_NAMESPACE::syslog(1 << 12, "invalid");
  EXPECT_EQ(sends, 1);
  LIBC_NAMESPACE::syslog(LOG_MAIL | LOG_ERR, "%s", "explicit");
  EXPECT_STREQ(datagram, "<19>test[123]: explicit");
  EXPECT_EQ(sockets, 1);
  LIBC_NAMESPACE::closelog();
  EXPECT_EQ(closes, 1);
}

TEST_F(LlvmLibcLocalLogTest, MissingReceiverAndConsoleFallback) {
  connect_error = ENOENT;
  LIBC_NAMESPACE::libc_errno = EDOM;
  LIBC_NAMESPACE::syslog(LOG_ERR, "absent");
  EXPECT_EQ(sends, 0);
  EXPECT_EQ(closes, 2);
  EXPECT_EQ(writes, 0);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  LIBC_NAMESPACE::openlog("fallback", LOG_CONS, LOG_USER);
  LIBC_NAMESPACE::syslog(LOG_ERR, "failure");
  EXPECT_EQ(consoles, 1);
  EXPECT_STREQ(fallback, "fallback: failure\r\n");
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
}

TEST_F(LlvmLibcLocalLogTest, InterruptFailureAndRecovery) {
  interrupt_send = true;
  LIBC_NAMESPACE::syslog(LOG_ERR, "retry");
  EXPECT_EQ(sends, 2);
  EXPECT_STREQ(datagram, "<11>test: retry");
  send_error = ECONNREFUSED;
  LIBC_NAMESPACE::syslog(LOG_ERR, "lost");
  EXPECT_EQ(closes, 2);
  send_error = 0;
  LIBC_NAMESPACE::syslog(LOG_ERR, "recovered");
  EXPECT_STREQ(datagram, "<11>test: recovered");
  EXPECT_EQ(sockets, 3);
}

TEST_F(LlvmLibcLocalLogTest, SocketFailureAndStderr) {
  socket_error = EAFNOSUPPORT;
  LIBC_NAMESPACE::openlog("stderr", LOG_PERROR, LOG_USER);
  LIBC_NAMESPACE::syslog(LOG_WARNING, "no sockets");
  EXPECT_EQ(connects, 0);
  EXPECT_EQ(sends, 0);
  EXPECT_EQ(closes, 0);
  EXPECT_STREQ(fallback, "stderr: no sockets\n");
}

TEST_F(LlvmLibcLocalLogTest, LongMessageAndErrnoExpansion) {
  char message[3000];
  for (size_t i = 0; i + 1 < sizeof(message); ++i)
    message[i] = 'a';
  message[sizeof(message) - 1] = '\0';
  LIBC_NAMESPACE::syslog(LOG_ERR, "%s", message);
  EXPECT_EQ(datagram[2999 + 10], '\0');
  LIBC_NAMESPACE::libc_errno = ENOENT;
  LIBC_NAMESPACE::syslog(LOG_ERR, "%m");
  EXPECT_STREQ(datagram, "<11>test: No such file or directory");
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), ENOENT);
}

TEST_F(LlvmLibcLocalLogTest, FormattingFailureDoesNotSendOrClobberErrno) {
  for (int failure = 1; failure <= 2; ++failure) {
    format_failure = failure;
    LIBC_NAMESPACE::libc_errno = EDOM;
    LIBC_NAMESPACE::syslog(LOG_ERR, "allocation failure");
    EXPECT_EQ(sockets, 0);
    EXPECT_EQ(writes, 0);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  }
}
