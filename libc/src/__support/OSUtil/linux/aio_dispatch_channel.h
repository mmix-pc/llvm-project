//===-- Bounded Linux AIO dispatch wake channel ------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_AIO_DISPATCH_CHANNEL_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_AIO_DISPATCH_CHANNEL_H

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/limits_macros.h"
#include "hdr/stdint_proxy.h"
#include "hdr/types/nfds_t.h"
#include "hdr/types/struct_pollfd.h"
#include "hdr/types/struct_timespec.h"
#include "include/llvm-libc-macros/poll-macros.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"
#include <stddef.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace aio_dispatch_channel {

// Nonthrowing checked values or positive errno, without libc_errno or public
// cancellation wrappers. No retries. Creation transfers one fd; close consumes
// it even on error. Injected operations must obey the same contracts and keep
// their backing state alive through cleanup.
struct LinuxOperations {
  LIBC_INLINE ErrorOr<int> eventfd(unsigned value, int flags) {
    return linux_syscalls::syscall_checked<int>(SYS_eventfd2, value, flags);
  }
  LIBC_INLINE ErrorOr<size_t> read(int fd, void *buffer, size_t size) {
    return linux_syscalls::syscall_checked<size_t>(SYS_read, fd, buffer, size);
  }
  LIBC_INLINE ErrorOr<size_t> write(int fd, const void *buffer, size_t size) {
    return linux_syscalls::syscall_checked<size_t>(SYS_write, fd, buffer, size);
  }
  LIBC_INLINE ErrorOr<int> close(int fd) {
    return linux_syscalls::syscall_checked<int>(SYS_close, fd);
  }
  LIBC_INLINE ErrorOr<int> ppoll(pollfd *fds, nfds_t count, timespec *timeout) {
#if defined(SYS_ppoll_time64)
    using KernelTime = __INT64_TYPE__;
    constexpr long NUMBER = SYS_ppoll_time64;
#else
    using KernelTime = long;
    constexpr long NUMBER = SYS_ppoll;
#endif
    struct KernelTimespec {
      KernelTime seconds;
      KernelTime nanoseconds;
    } copy = {};
    if (timeout) {
      if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 ||
          timeout->tv_nsec >= 1000000000)
        return Error(EINVAL);
#if !defined(SYS_ppoll_time64)
      if (timeout->tv_sec > LONG_MAX)
        return Error(EOVERFLOW);
#endif
      copy.seconds = static_cast<KernelTime>(timeout->tv_sec);
      copy.nanoseconds = static_cast<KernelTime>(timeout->tv_nsec);
    }
    // No signal-mask change. Raw ppoll may overwrite its timeout argument.
    return linux_syscalls::syscall_checked<int>(
        NUMBER, fds, count, timeout ? &copy : nullptr, nullptr, size_t(8));
  }
};

// Owns only a nonblocking, close-on-exec eventfd, never the ring or requests.
// Externally serialize initialize/reset/destruction against all users, and
// wait against wait. Wake may run concurrently with wait while both fds stay
// stable; injected operations must support that concurrency. No scheduling,
// submission, ring consumption, signal-mask changes or drain loops occur here.
template <class Operations = LinuxOperations> class Channel {
  Operations operations;
  int control = -1;

public:
  LIBC_INLINE explicit Channel(Operations operations = {})
      : operations(operations) {}
  Channel(const Channel &) = delete;
  Channel &operator=(const Channel &) = delete;
  Channel(Channel &&) = delete;
  Channel &operator=(Channel &&) = delete;
  LIBC_INLINE ~Channel() { (void)reset(); }

  // EBUSY leaves the live channel untouched. A failed creation leaves nothing
  // to close, so cleanup cannot replace the initialization error.
  LIBC_INLINE ErrorOr<void> initialize() {
    if (control >= 0)
      return Error(EBUSY);
    auto created = operations.eventfd(0, O_NONBLOCK | O_CLOEXEC);
    if (!created)
      return Error(created.error());
    if (*created < 0)
      return Error(EPROTO);
    control = *created;
    return {};
  }

  // Consume ownership before one close attempt, including on EINTR.
  LIBC_INLINE ErrorOr<void> reset() {
    if (control < 0)
      return {};
    const int fd = control;
    control = -1;
    auto result = operations.close(fd);
    if (!result)
      return Error(result.error());
    return {};
  }

  LIBC_INLINE int control_fd() const { return control; } // Borrowed only.

  LIBC_INLINE ErrorOr<void> wake() {
    if (control < 0)
      return Error(EBADF);
    const uint64_t value = 1;
    auto result = operations.write(control, &value, sizeof(value));
    if (!result) {
      if (result.error() == EAGAIN)
        return {};
      return Error(result.error());
    }
    if (*result != sizeof(value))
      return Error(EPROTO);
    return {};
  }

  // One poll and at most one eventfd counter read. Readiness (including a
  // stale wake yielding EAGAIN) and timeout both succeed: the caller always
  // runs one dispatcher step. EINTR and other errors are never retried.
  LIBC_INLINE ErrorOr<void> wait(int borrowed_ring_fd,
                                 const timespec *timeout) {
    if (control < 0 || borrowed_ring_fd < 0 || borrowed_ring_fd == control)
      return Error(EBADF);
    return wait_impl(borrowed_ring_fd, timeout);
  }

  // Maintenance workers need control/timeout wakeups, never CQ readiness.
  LIBC_INLINE ErrorOr<void> wait_control(const timespec *timeout) {
    if (control < 0)
      return Error(EBADF);
    return wait_impl(-1, timeout);
  }

private:
  LIBC_INLINE ErrorOr<void> wait_impl(int borrowed_ring_fd,
                                      const timespec *timeout) {
    timespec copy = {};
    if (timeout)
      copy = *timeout;
    pollfd fds[2] = {{control, POLLIN, 0}, {borrowed_ring_fd, POLLIN, 0}};
    const nfds_t count = borrowed_ring_fd < 0 ? 1 : 2;
    auto result = operations.ppoll(fds, count, timeout ? &copy : nullptr);
    if (!result)
      return Error(result.error());
    if (*result < 0 || nfds_t(*result) > count)
      return Error(EPROTO);
    const int events = fds[0].revents | fds[1].revents;
    if (events & POLLNVAL)
      return Error(EBADF);
    if (events & (POLLERR | POLLHUP))
      return Error(EIO);
    if (!*result || !(fds[0].revents & POLLIN))
      return {};
    uint64_t value = 0;
    auto read = operations.read(control, &value, sizeof(value));
    if (!read) {
      if (read.error() == EAGAIN)
        return {};
      return Error(read.error());
    }
    if (*read != sizeof(value))
      return Error(EPROTO);
    return {};
  }
};

} // namespace aio_dispatch_channel
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_AIO_DISPATCH_CHANNEL_H
