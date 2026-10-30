//===-- Private Linux notification channels ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_NOTIFICATION_CHANNELS_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_NOTIFICATION_CHANNELS_H

#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "hdr/stdint_proxy.h"
#include "hdr/sys_socket_macros.h"
#include "hdr/types/struct_iovec.h"
#include "hdr/types/struct_msghdr.h"
#include "src/__support/OSUtil/linux/notification_transport.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"
#include <linux/fcntl.h>
#include <linux/netlink.h>
#include <linux/signalfd.h>
#include <stddef.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace notification_channels {

// Checked values or positive errno, without libc_errno or public cancellation
// wrappers. Each operation makes exactly one syscall. A successful creation
// transfers one new fd; close consumes it even on error. Injected operations
// must obey these contracts and keep their backing state alive through cleanup.
struct LinuxOperations {
  LIBC_INLINE ErrorOr<int> eventfd(unsigned value, int flags) {
    return linux_syscalls::syscall_checked<int>(SYS_eventfd2, value, flags);
  }
  LIBC_INLINE ErrorOr<int> signalfd(int fd, const uint64_t *mask, size_t size,
                                    int flags) {
    return linux_syscalls::syscall_checked<int>(SYS_signalfd4, fd, mask, size,
                                                flags);
  }
  LIBC_INLINE ErrorOr<int> socket(int domain, int type, int protocol) {
    return linux_syscalls::syscall_checked<int>(SYS_socket, domain, type,
                                                protocol);
  }
  LIBC_INLINE ErrorOr<size_t> read(int fd, void *buffer, size_t size) {
    return linux_syscalls::syscall_checked<size_t>(SYS_read, fd, buffer, size);
  }
  LIBC_INLINE ErrorOr<size_t> write(int fd, const void *buffer, size_t size) {
    return linux_syscalls::syscall_checked<size_t>(SYS_write, fd, buffer, size);
  }
  LIBC_INLINE ErrorOr<size_t> recvmsg(int fd, msghdr *message, int flags) {
    return linux_syscalls::syscall_checked<size_t>(SYS_recvmsg, fd, message,
                                                   flags);
  }
  LIBC_INLINE ErrorOr<int> close(int fd) {
    return linux_syscalls::syscall_checked<int>(SYS_close, fd);
  }
};

// Owns only fds, never helpers, registrations or callbacks. The caller reserves
// and blocks the notification signal in the appropriate threads, and performs
// timer reads in the directed helper. No signal masks or providers are changed.
// Serialize initialization, lazy MQ creation and reset/destruction against all
// users (including borrowed fd users); serialize readers of each channel. Wake
// may run concurrently with reads while the fds remain stable. Injected ops
// must support any such concurrency. There are no retries or scheduling
// decisions.
template <class Operations = LinuxOperations> class Channels {
  Operations operations;
  int control = -1;
  int timer = -1;
  int mq = -1;

  LIBC_INLINE ErrorOr<void> fail(int error) {
    (void)reset();
    return Error(error);
  }

public:
  LIBC_INLINE explicit Channels(Operations operations = {})
      : operations(operations) {}
  Channels(const Channels &) = delete;
  Channels &operator=(const Channels &) = delete;
  Channels(Channels &&) = delete;
  Channels &operator=(Channels &&) = delete;
  LIBC_INLINE ~Channels() { (void)reset(); }

  // Creates control and timer channels only. The kernel mask is exactly 64
  // bits, not public sigset_t. Signal selection belongs to the caller; only
  // out-of-range and uncatchable signals are rejected. EBUSY leaves live fds
  // alone. Any creation failure rolls back this initialization, preserving the
  // original error even if cleanup fails. A later initialize may retry.
  LIBC_INLINE ErrorOr<void> initialize(int notification_signal) {
    if (control >= 0 || timer >= 0 || mq >= 0)
      return Error(EBUSY);
    if (notification_signal <= 0 || notification_signal > 64 ||
        notification_signal == SIGKILL || notification_signal == SIGSTOP)
      return Error(EINVAL);

    // eventfd2 uses O_* values for EFD_NONBLOCK/EFD_CLOEXEC; no semaphore mode.
    auto created = operations.eventfd(0, O_NONBLOCK | O_CLOEXEC);
    if (!created)
      return Error(created.error());
    control = *created;
    if (control < 0)
      return fail(EPROTO);

    const uint64_t mask = uint64_t(1) << (notification_signal - 1);
    created = operations.signalfd(-1, &mask, sizeof(mask),
                                  SFD_NONBLOCK | SFD_CLOEXEC);
    if (!created)
      return fail(created.error());
    timer = *created;
    if (timer < 0)
      return fail(EPROTO);
    return {};
  }

  // Idempotent lazy creation; failure leaves the already initialized control
  // and timer channels intact, with no MQ fd owned. No bind/connect is needed:
  // Linux ipc/mqueue.c delivers directly to the socket referenced by the fd.
  // glibc sysdeps/unix/sysv/linux/mq_notify.c::init_mq_netlink uses AF_NETLINK,
  // SOCK_RAW | SOCK_CLOEXEC, protocol 0 (NETLINK_ROUTE), unbound. Linux
  // net/socket.c also accepts SOCK_NONBLOCK atomically at creation.
  LIBC_INLINE ErrorOr<void> ensure_mq() {
    if (control < 0 || timer < 0)
      return Error(EBADF);
    if (mq >= 0)
      return {};
    auto created = operations.socket(
        AF_NETLINK, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (!created)
      return Error(created.error());
    if (*created < 0)
      return Error(EPROTO);
    mq = *created;
    return {};
  }

  // Close every owned fd once, in reverse creation order. Even EINTR consumes
  // the fd; neither reset nor destruction may retry it. Return the first error.
  LIBC_INLINE ErrorOr<void> reset() {
    int error = 0;
    int *descriptors[] = {&mq, &timer, &control};
    for (int *descriptor : descriptors) {
      if (*descriptor < 0)
        continue;
      const int fd = *descriptor;
      *descriptor = -1;
      auto result = operations.close(fd);
      if (!result && !error)
        error = result.error();
    }
    if (error)
      return Error(error);
    return {};
  }

  // Borrowed descriptors for polling/registration, never ownership transfer.
  LIBC_INLINE int control_fd() const { return control; }
  LIBC_INLINE int timer_fd() const { return timer; }
  LIBC_INLINE int mq_fd() const { return mq; }

  // Saturation means a wake is already pending. All other errors, including
  // EINTR, are returned unchanged for caller scheduling. Short I/O is EPROTO.
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

  // All reads stage into local storage: every failure leaves output unchanged,
  // although a malformed record may already have been consumed from the fd.
  LIBC_INLINE ErrorOr<void> read_control(uint64_t &output) {
    if (control < 0)
      return Error(EBADF);
    uint64_t value = 0;
    auto result = operations.read(control, &value, sizeof(value));
    if (!result)
      return Error(result.error());
    if (*result != sizeof(value))
      return Error(EPROTO);
    output = value;
    return {};
  }

  // Exactly one 128-byte raw UAPI record. This does not establish ownership:
  // the caller must resolve the opaque integer token in ssi_ptr and validate
  // signal, SI_TIMER and the live kernel timer ID with decode_timer under its
  // registry gate. Never interpret a kernel cookie as a user-space owner
  // pointer.
  LIBC_INLINE ErrorOr<void> read_timer(signalfd_siginfo &output) {
    if (timer < 0)
      return Error(EBADF);
    signalfd_siginfo info = {};
    auto result = operations.read(timer, &info, sizeof(info));
    if (!result)
      return Error(result.error());
    if (*result != sizeof(info))
      return Error(EPROTO);
    output = info;
    return {};
  }

  // One raw MQ cookie, not nlmsghdr framing. The parser alone checks kernel
  // source, returned lengths, truncation and cookie format. A valid token still
  // needs registry validation by the caller. Reading never creates the socket.
  LIBC_INLINE ErrorOr<void>
  read_mq(notification_transport::Notification &output) {
    if (mq < 0)
      return Error(EBADF);
    unsigned char cookie[notification_transport::MQ_COOKIE_SIZE] = {};
    sockaddr_nl source = {};
    iovec data = {cookie, sizeof(cookie)};
    msghdr message = {};
    message.msg_name = &source;
    message.msg_namelen = sizeof(source);
    message.msg_iov = &data;
    message.msg_iovlen = 1;
    auto result = operations.recvmsg(mq, &message, MSG_DONTWAIT);
    if (!result)
      return Error(result.error());
    if (!notification_transport::decode_mq(cookie, *result, message.msg_flags,
                                           &source, message.msg_namelen,
                                           output))
      return Error(EPROTO);
    return {};
  }
};

} // namespace notification_channels
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_NOTIFICATION_CHANNELS_H
