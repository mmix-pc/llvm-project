//===-- Private Linux notification transport --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_NOTIFICATION_TRANSPORT_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_NOTIFICATION_TRANSPORT_H

#include "hdr/signal_macros.h"
#include "hdr/stdint_proxy.h"
#include "hdr/sys_socket_macros.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"
#include <linux/netlink.h>
#include <linux/signalfd.h>
#include <stddef.h>

namespace LIBC_NAMESPACE_DECL {
namespace notification_transport {

// From Linux include/uapi/linux/mqueue.h. Do not include that header here: it
// also defines mq_attr, which conflicts with the public libc type.
constexpr size_t MQ_COOKIE_SIZE = 32;
constexpr unsigned char MQ_WOKENUP = 1;
constexpr unsigned char MQ_REMOVED = 2;

enum class Event { MqWokenUp, MqRemoved, TimerExpired };

struct Notification {
  uint64_t token;
  Event event;
  uint32_t overrun; // Kernel timer overrun count; zero for MQ.
};

static_assert(sizeof(signalfd_siginfo) == 128);
static_assert(offsetof(signalfd_siginfo, ssi_signo) == 0);
static_assert(offsetof(signalfd_siginfo, ssi_code) == 8);
static_assert(offsetof(signalfd_siginfo, ssi_tid) == 24);
static_assert(offsetof(signalfd_siginfo, ssi_overrun) == 32);
static_assert(offsetof(signalfd_siginfo, ssi_ptr) == 48);
static_assert(sizeof(signalfd_siginfo::ssi_ptr) == sizeof(uint64_t));

// No syscalls, allocation, registry lookup or callback dispatch. Inputs must
// remain stable during validation. All failures leave output unchanged.
// The registration cookie uses big-endian bytes 0..7, zero bytes 8..31.
// Linux overwrites byte 31 with the terminal status when sending it.
LIBC_INLINE bool encode_mq_cookie(uint64_t token,
                                  unsigned char (&cookie)[MQ_COOKIE_SIZE]) {
  if (token == 0)
    return false;
  for (size_t i = 0; i < 8; ++i)
    cookie[i] = static_cast<unsigned char>(token >> (56 - 8 * i));
  for (size_t i = 8; i < MQ_COOKIE_SIZE; ++i)
    cookie[i] = 0;
  return true;
}

// Pass recvmsg's returned data length, msg_flags, source and msg_namelen, with
// storage for the entire sockaddr_nl. ipc/mqueue.c sends the raw cookie, not an
// nlmsghdr. alloc_skb clears its control block; netlink_attachskb/sendskb leave
// portid/dst_group zero. net/netlink/af_netlink.c::netlink_recvmsg therefore
// returns AF_NETLINK, pad=0, pid=0, groups=0, sizeof(sockaddr_nl).
LIBC_INLINE bool decode_mq(const unsigned char *cookie, size_t length,
                           int message_flags, const sockaddr_nl *source,
                           size_t source_length, Notification &output) {
  if (!cookie || length != MQ_COOKIE_SIZE || (message_flags & MSG_TRUNC) ||
      !source || source_length != sizeof(sockaddr_nl))
    return false;
  if (source->nl_family != AF_NETLINK || source->nl_pad != 0 ||
      source->nl_pid != 0 || source->nl_groups != 0)
    return false;

  const unsigned char status = cookie[MQ_COOKIE_SIZE - 1];
  if (status != MQ_WOKENUP && status != MQ_REMOVED)
    return false;
  for (size_t i = 8; i < MQ_COOKIE_SIZE - 1; ++i)
    if (cookie[i] != 0)
      return false;
  uint64_t token = 0;
  for (size_t i = 0; i < 8; ++i)
    token = (token << 8) | cookie[i];
  if (token == 0)
    return false;

  output = {token, status == MQ_WOKENUP ? Event::MqWokenUp : Event::MqRemoved,
            0};
  return true;
}

// Validate exactly one record read from the service's signalfd, never a public
// siginfo_t. The caller supplies its reserved signal (34 on MMIX) and the live
// registration's Linux timer ID, not a public timer_t or helper TID. It must
// still establish live token/ID association under its registry/operation gate
// before dispatch; transport validity alone does not establish ownership.
// fs/signalfd.c copies si_tid/si_ptr/si_overrun into these UAPI fields.
LIBC_INLINE bool decode_timer(const signalfd_siginfo *info, size_t length,
                              int notification_signal, int kernel_timer_id,
                              Notification &output) {
  if (!info || length != sizeof(signalfd_siginfo) || notification_signal <= 0 ||
      kernel_timer_id < 0)
    return false;
  if (info->ssi_signo != static_cast<uint32_t>(notification_signal) ||
      info->ssi_code != SI_TIMER || info->ssi_ptr == 0 ||
      info->ssi_tid != static_cast<uint32_t>(kernel_timer_id))
    return false;

  output = {info->ssi_ptr, Event::TimerExpired, info->ssi_overrun};
  return true;
}

} // namespace notification_transport
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_NOTIFICATION_TRANSPORT_H
