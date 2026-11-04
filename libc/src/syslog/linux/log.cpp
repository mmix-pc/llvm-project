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
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/mutex.h"
#include "src/fcntl/open.h"
#include "src/stdio/asprintf.h"
#include "src/stdio/vasprintf.h"
#include "src/sys/socket/connect.h"
#include "src/sys/socket/send.h"
#include "src/sys/socket/socket.h"
#include "src/time/localtime_r.h"
#include "src/time/time_func.h"
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

// Temporary ownership survives cancellation unwinding while the lock is held.
struct Buffer {
  char *data = nullptr;
  ~Buffer() { ::free(data); }
};

struct Descriptor {
  int fd = -1;
  ~Descriptor() {
    // A destructor cannot be its owner's last cancellation point: unwinding
    // from close would abandon the descriptor without running us again.
    if (fd >= 0)
      (void)linux_syscalls::close(fd);
  }
};

// Sender timestamp: local timezone, but independent of LC_TIME.
bool timestamp(char (&text)[16]) {
  time_t now = LIBC_NAMESPACE::time(nullptr);
  struct tm local;
  if (now == time_t(-1) || !LIBC_NAMESPACE::localtime_r(&now, &local))
    return false;
  constexpr char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  if (local.tm_mon < 0 || local.tm_mon > 11)
    return false;
  for (int i = 0; i < 3; ++i)
    text[i] = months[local.tm_mon * 3 + i];
  text[3] = text[6] = ' ';
  text[4] = local.tm_mday < 10 ? ' ' : char('0' + local.tm_mday / 10);
  text[5] = '0' + local.tm_mday % 10;
  text[7] = char('0' + local.tm_hour / 10);
  text[8] = '0' + local.tm_hour % 10;
  text[9] = text[12] = ':';
  text[10] = char('0' + local.tm_min / 10);
  text[11] = '0' + local.tm_min % 10;
  text[13] = char('0' + local.tm_sec / 10);
  text[14] = '0' + local.tm_sec % 10;
  text[15] = '\0';
  return true;
}

void disconnect() {
  // Keep ownership if close unwinds before consuming the descriptor. Linux
  // close consumes it even on error; after a return, never retry that number.
  if (descriptor >= 0)
    LIBC_NAMESPACE::close(descriptor);
  descriptor = -1;
}

bool connect() {
  if (descriptor >= 0)
    return true;
  Descriptor pending;
  pending.fd = LIBC_NAMESPACE::socket(
      AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (pending.fd < 0)
    return false;
  const sockaddr_un address = {AF_UNIX, "/dev/log"};
  if (LIBC_NAMESPACE::connect(pending.fd,
                             reinterpret_cast<const sockaddr *>(&address),
                             offsetof(sockaddr_un, sun_path) + 9) == 0) {
    descriptor = pending.fd;
    pending.fd = -1;
    return true;
  }
  return false;
}

// Only a stale connection warrants one reconnect; never retry queue pressure,
// interruption, oversized messages, permissions or resource exhaustion.
bool stale_connection(int error) {
  return error == ECONNREFUSED || error == ECONNRESET || error == ENOTCONN;
}

bool send(const char *message, size_t size) {
  if (!connect())
    return false;
  ssize_t result = LIBC_NAMESPACE::send(descriptor, message, size, MSG_NOSIGNAL);
  if (result < 0 && stale_connection(libc_errno)) {
    disconnect();
    if (!connect())
      return false;
    result = LIBC_NAMESPACE::send(descriptor, message, size, MSG_NOSIGNAL);
    if (result < 0 && stale_connection(libc_errno))
      disconnect();
  }
  return result >= 0 && static_cast<size_t>(result) == size;
}

void write_all(int fd, const char *message, size_t size) {
  while (size) {
    ssize_t result = LIBC_NAMESPACE::write(fd, message, size);
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
  char date[16];
  if (!timestamp(date))
    return;
  Buffer body;
  // %m must observe the caller's errno, not socket or formatting setup errors.
  libc_errno = saved.value;
  int body_size = LIBC_NAMESPACE::vasprintf(&body.data, format, args);
  if (body_size < 0)
    return;
  const char *ident = tag ? tag : LOG_PROGRAM_NAME;
  if (!ident)
    ident = "";
  Buffer message;
  int size;
  if (options & LOG_PID)
    size = LIBC_NAMESPACE::asprintf(&message.data, "<%d>%s %s[%d]: %*s", priority,
                                   date, ident,
                                   static_cast<int>(LIBC_NAMESPACE::getpid()),
                                   body_size, "");
  else
    size = LIBC_NAMESPACE::asprintf(&message.data, "<%d>%s %s: %*s", priority,
                                   date, ident, body_size, "");
  if (size < 0)
    return;
  // Reserve the body by length, then copy its bytes, including embedded NULs.
  for (int i = 0; i < body_size; ++i)
    message.data[size - body_size + i] = body.data[i];

  // The datagram excludes the formatting buffer's terminating NUL.
  bool delivered = send(message.data, static_cast<size_t>(size));
  const char *text = message.data;
  while (*text && *text != '>')
    ++text;
  if (*text)
    ++text;
  size_t text_size = static_cast<size_t>(size) - (text - message.data);
  if (options & LOG_PERROR) {
    write_all(2, text, text_size);
    write_all(2, "\n", 1);
  }
  if (!delivered && (options & LOG_CONS)) {
    Descriptor console;
    console.fd = LIBC_NAMESPACE::open(
        "/dev/console", O_WRONLY | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
    if (console.fd >= 0) {
      write_all(console.fd, text, text_size);
      write_all(console.fd, "\r\n", 2);
    }
  }
}
} // namespace local_log

namespace internal {
bool try_lock_log_for_fork() {
  return local_log::mutex.try_lock() == MutexError::NONE;
}

void finish_log_fork(bool child) {
  if (child) {
    int old = local_log::descriptor;
    local_log::descriptor = -1;
    // Snapshot restoration cannot enter cancellation or retry a consumed fd.
    if (old >= 0)
      (void)linux_syscalls::close(old);
  }
  if (local_log::mutex.unlock() != MutexError::NONE)
    __builtin_trap();
}
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
