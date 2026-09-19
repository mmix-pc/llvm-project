//===-- Linux local syslog transport -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/syslog/log.h"
#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/func/free.h"
#include "hdr/sys_socket_macros.h"
#include "hdr/syslog_macros.h"
#include "hdr/types/struct_sockaddr_un.h"
#include "src/__support/CPP/mutex.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/mutex.h"
#include "src/fcntl/open.h"
#include "src/stdio/asprintf.h"
#include "src/stdio/vasprintf.h"
#include "src/sys/socket/connect.h"
#include "src/sys/socket/send.h"
#include "src/sys/socket/socket.h"
#include "src/unistd/close.h"
#include "src/unistd/getpid.h"
#include "src/unistd/write.h"
#include <stddef.h>

#ifdef LIBC_FULL_BUILD
#include "src/errno/program_invocation_short_name.h"
#define LOG_PROGRAM_NAME LIBC_NAMESPACE::program_invocation_short_name
#else
extern "C" char *program_invocation_short_name;
#define LOG_PROGRAM_NAME ::program_invocation_short_name
#endif

namespace LIBC_NAMESPACE_DECL {
namespace local_log {
namespace {
Mutex mutex(false, false, false, false);
const char *tag = nullptr;
int options = 0;
int facility = LOG_USER;
int mask = 0xff;
int descriptor = -1;

struct PreserveErrno {
  int value = libc_errno;
  ~PreserveErrno() { libc_errno = value; }
};

void disconnect() {
  if (descriptor >= 0)
    LIBC_NAMESPACE::close(descriptor);
  descriptor = -1;
}

bool connect() {
  if (descriptor >= 0)
    return true;
  descriptor = LIBC_NAMESPACE::socket(
      AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (descriptor < 0)
    return false;
  const sockaddr_un address = {AF_UNIX, "/dev/log"};
  if (LIBC_NAMESPACE::connect(descriptor,
                              reinterpret_cast<const sockaddr *>(&address),
                              offsetof(sockaddr_un, sun_path) + 9) == 0)
    return true;
  disconnect();
  return false;
}

bool send(const char *message, size_t size) {
  if (!connect())
    return false;
  ssize_t result;
  do {
    result = LIBC_NAMESPACE::send(descriptor, message, size, MSG_NOSIGNAL);
  } while (result < 0 && libc_errno == EINTR);
  return result >= 0 && static_cast<size_t>(result) == size;
}

void write_all(int fd, const char *message, size_t size) {
  while (size) {
    ssize_t result = LIBC_NAMESPACE::write(fd, message, size);
    if (result < 0 && libc_errno == EINTR)
      continue;
    if (result <= 0)
      return;
    message += result;
    size -= static_cast<size_t>(result);
  }
}
} // namespace

void open(const char *ident, int new_options, int new_facility) {
  PreserveErrno saved;
  cpp::lock_guard lock(mutex);
  if (ident)
    tag = ident;
  options = new_options;
  if (new_facility >= 0 && new_facility <= LOG_LOCAL7 && !(new_facility & 7))
    facility = new_facility;
  if (options & LOG_NDELAY)
    connect();
}

void close() {
  PreserveErrno saved;
  cpp::lock_guard lock(mutex);
  disconnect();
  tag = nullptr;
}

int set_mask(int new_mask) {
  cpp::lock_guard lock(mutex);
  int previous = mask;
  if (new_mask)
    mask = new_mask;
  return previous;
}

void write(int priority, const char *format, va_list args) {
  PreserveErrno saved;
  cpp::lock_guard lock(mutex);
  if (priority < 0 || (priority & ~0x3ff) || (priority & ~7) > LOG_LOCAL7 ||
      !(mask & LOG_MASK(priority & 7)))
    return;
  if (!(priority & ~7))
    priority |= facility;
  char *body = nullptr;
  // %m must observe the caller's errno, not socket or formatting setup errors.
  libc_errno = saved.value;
  int body_size = LIBC_NAMESPACE::vasprintf(&body, format, args);
  if (body_size < 0)
    return;
  const char *ident = tag ? tag : LOG_PROGRAM_NAME;
  if (!ident)
    ident = "";
  char *message = nullptr;
  int size;
  if (options & LOG_PID)
    size = LIBC_NAMESPACE::asprintf(&message, "<%d>%s[%d]: %s", priority, ident,
                                    static_cast<int>(LIBC_NAMESPACE::getpid()),
                                    body);
  else
    size =
        LIBC_NAMESPACE::asprintf(&message, "<%d>%s: %s", priority, ident, body);
  ::free(body);
  if (size < 0)
    return;

  // The receiver supplies the timestamp. The datagram has no terminating NUL.
  bool delivered = send(message, static_cast<size_t>(size));
  if (!delivered) {
    disconnect();
    delivered = send(message, static_cast<size_t>(size));
    if (!delivered)
      disconnect();
  }
  const char *text = message;
  while (*text && *text != '>')
    ++text;
  if (*text)
    ++text;
  size_t text_size = static_cast<size_t>(size) - (text - message);
  if (options & LOG_PERROR) {
    write_all(2, text, text_size);
    write_all(2, "\n", 1);
  }
  if (!delivered && (options & LOG_CONS)) {
    int console = LIBC_NAMESPACE::open(
        "/dev/console", O_WRONLY | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
    if (console >= 0) {
      write_all(console, text, text_size);
      write_all(console, "\r\n", 2);
      LIBC_NAMESPACE::close(console);
    }
  }
  ::free(message);
}
} // namespace local_log
} // namespace LIBC_NAMESPACE_DECL
