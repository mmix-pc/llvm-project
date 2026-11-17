//===-- Linux interface address enumeration ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC_NET_LINUX_IFADDRS_IMPL_H
#define LLVM_LIBC_SRC_NET_LINUX_IFADDRS_IMPL_H

#include "hdr/errno_macros.h"
#include "hdr/net_if_macros.h"
#include "hdr/stdint_proxy.h"
#include "hdr/sys_socket_macros.h"
#include "hdr/types/socklen_t.h"
#include "hdr/types/ssize_t.h"
#include "hdr/types/struct_ifaddrs.h"
#include "hdr/types/struct_sockaddr_in.h"
#include "hdr/types/struct_sockaddr_in6.h"
#include "src/__support/CPP/new.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"
#include <linux/if.h>
#include <linux/if_addr.h>
#include <linux/if_link.h>
#include <linux/if_packet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

namespace LIBC_NAMESPACE_DECL {
namespace net::ifaddrs_detail {

LIBC_INLINE const void *attribute_data(const rtattr *attribute) {
  return reinterpret_cast<const unsigned char *>(attribute) + RTA_LENGTH(0);
}
LIBC_INLINE const void *message_data(const nlmsghdr *message) {
  return reinterpret_cast<const unsigned char *>(message) + NLMSG_HDRLEN;
}

inline void copy(void *to, const void *from, size_t count) {
  auto *out = static_cast<unsigned char *>(to);
  auto *in = static_cast<const unsigned char *>(from);
  for (size_t i = 0; i < count; ++i)
    out[i] = in[i];
}

// Linux bounds hardware addresses by MAX_ADDR_LEN (32). sockaddr_ll's public
// prefix is retained, with enough trailing storage for non-Ethernet devices.
struct PacketAddress {
  unsigned short family, protocol;
  int index;
  unsigned short hardware_type;
  unsigned char packet_type, length;
  unsigned char address[32];
};
static_assert(__builtin_offsetof(PacketAddress, address) ==
              __builtin_offsetof(sockaddr_ll, sll_addr));
union Address {
  sockaddr_in v4;
  sockaddr_in6 v6;
  PacketAddress packet;
};
struct Node {
  ifaddrs value;
  char name[IF_NAMESIZE];
  Address address, mask, peer;
  rtnl_link_stats statistics;
  int index;
  bool link;
};
static_assert(__builtin_offsetof(Node, value) == 0);

template <typename Policy> struct List {
  ifaddrs *head = nullptr;
  ifaddrs **tail = &head;
  ~List() {
    while (head) {
      ifaddrs *next = head->ifa_next;
      Policy::deallocate(head);
      head = next;
    }
  }
  Node *append() {
    void *storage = Policy::allocate(sizeof(Node));
    if (!storage)
      return nullptr;
    Node *node = ::new (storage) Node{};
    node->value.ifa_name = node->name;
    *tail = &node->value;
    tail = &node->value.ifa_next;
    return node;
  }
  Node *find(unsigned index) {
    for (ifaddrs *item = head; item; item = item->ifa_next) {
      auto *node = reinterpret_cast<Node *>(item);
      if (node->link && static_cast<unsigned>(node->index) == index)
        return node;
    }
    return nullptr;
  }
  ifaddrs *release() {
    ifaddrs *result = head;
    head = nullptr;
    return result;
  }
};

inline int name(char *destination, const rtattr *attribute) {
  if (!attribute)
    return EINVAL;
  size_t count = RTA_PAYLOAD(attribute);
  auto *source = static_cast<const char *>(attribute_data(attribute));
  for (size_t i = 0; i < count && i < IF_NAMESIZE; ++i) {
    destination[i] = source[i];
    if (!source[i])
      return i ? 0 : EINVAL;
  }
  return EINVAL;
}

template <size_t Count>
int attributes(const unsigned char *bytes, size_t size,
               const rtattr *(&result)[Count]) {
  while (size) {
    if (size < sizeof(rtattr))
      return EINVAL;
    auto *attribute = reinterpret_cast<const rtattr *>(bytes);
    size_t length = attribute->rta_len;
    if (length < sizeof(rtattr) || length > size)
      return EINVAL;
    unsigned type = attribute->rta_type & NLA_TYPE_MASK;
    if (type < Count)
      result[type] = attribute;
    size_t step = RTA_ALIGN(length);
    if (step > size)
      return length == size ? 0 : EINVAL;
    size -= step;
    bytes += step;
  }
  return 0;
}

inline int packet(Address &destination, const rtattr *attribute,
                  const ifinfomsg &link) {
  size_t length = RTA_PAYLOAD(attribute);
  if (length > sizeof(destination.packet.address))
    return EOVERFLOW;
  destination.packet = {};
  destination.packet.family = AF_PACKET;
  destination.packet.index = link.ifi_index;
  destination.packet.hardware_type = link.ifi_type;
  destination.packet.length = static_cast<unsigned char>(length);
  copy(destination.packet.address, attribute_data(attribute), length);
  return 0;
}

template <typename Policy>
int link(const nlmsghdr *message, List<Policy> &list) {
  if (message->nlmsg_len < NLMSG_LENGTH(sizeof(ifinfomsg)))
    return EINVAL;
  const auto &info = *static_cast<const ifinfomsg *>(message_data(message));
  if (info.ifi_index <= 0)
    return EINVAL;
  const rtattr *values[IFLA_MAX + 1] = {};
  int error =
      attributes(reinterpret_cast<const unsigned char *>(&info) +
                     NLMSG_ALIGN(sizeof(info)),
                 message->nlmsg_len - NLMSG_LENGTH(sizeof(info)), values);
  if (error)
    return error;
  Node *node = list.append();
  if (!node)
    return ENOMEM;
  node->link = true;
  node->index = info.ifi_index;
  node->value.ifa_flags = info.ifi_flags;
  if ((error = name(node->name, values[IFLA_IFNAME])))
    return error;
  if (values[IFLA_ADDRESS]) {
    if ((error = packet(node->address, values[IFLA_ADDRESS], info)))
      return error;
    node->value.ifa_addr = reinterpret_cast<sockaddr *>(&node->address.packet);
  }
  if (values[IFLA_BROADCAST]) {
    if ((error = packet(node->peer, values[IFLA_BROADCAST], info)))
      return error;
    node->value.ifa_broadaddr =
        reinterpret_cast<sockaddr *>(&node->peer.packet);
  }
  if (values[IFLA_STATS] &&
      RTA_PAYLOAD(values[IFLA_STATS]) >= sizeof(node->statistics)) {
    copy(&node->statistics, attribute_data(values[IFLA_STATS]),
         sizeof(node->statistics));
    node->value.ifa_data = &node->statistics;
  }
  return 0;
}

inline sockaddr *ip(Address &destination, int family, const void *bytes,
                    unsigned index) {
  if (family == AF_INET) {
    destination.v4 = {};
    destination.v4.sin_family = AF_INET;
    copy(&destination.v4.sin_addr, bytes, 4);
    return reinterpret_cast<sockaddr *>(&destination.v4);
  }
  destination.v6 = {};
  destination.v6.sin6_family = AF_INET6;
  copy(&destination.v6.sin6_addr, bytes, 16);
  const unsigned char *address = static_cast<const unsigned char *>(bytes);
  if ((address[0] == 0xfe && (address[1] & 0xc0) == 0x80) ||
      (address[0] == 0xff && (address[1] & 0xf) == 2))
    destination.v6.sin6_scope_id = index;
  return reinterpret_cast<sockaddr *>(&destination.v6);
}

template <typename Policy>
int address(const nlmsghdr *message, List<Policy> &list) {
  if (message->nlmsg_len < NLMSG_LENGTH(sizeof(ifaddrmsg)))
    return EINVAL;
  const auto &info = *static_cast<const ifaddrmsg *>(message_data(message));
  if (info.ifa_family != AF_INET && info.ifa_family != AF_INET6)
    return 0;
  unsigned width = info.ifa_family == AF_INET ? 4 : 16;
  if (info.ifa_prefixlen > width * 8)
    return EINVAL;
  const rtattr *values[IFA_MAX + 1] = {};
  int error =
      attributes(reinterpret_cast<const unsigned char *>(&info) +
                     NLMSG_ALIGN(sizeof(info)),
                 message->nlmsg_len - NLMSG_LENGTH(sizeof(info)), values);
  if (error)
    return error;
  constexpr unsigned address_types[] = {IFA_ADDRESS, IFA_LOCAL, IFA_BROADCAST};
  for (unsigned type : address_types)
    if (values[type] && RTA_PAYLOAD(values[type]) != width)
      return EINVAL;
  const rtattr *local =
      values[IFA_LOCAL] ? values[IFA_LOCAL] : values[IFA_ADDRESS];
  if (!local)
    return EINVAL;
  Node *owner = list.find(info.ifa_index);
  if (!owner)
    return EAGAIN;
  Node *node = list.append();
  if (!node)
    return ENOMEM;
  node->index = owner->index;
  node->value.ifa_flags = owner->value.ifa_flags;
  copy(node->name, owner->name, sizeof(node->name));
  if (values[IFA_LABEL] && (error = name(node->name, values[IFA_LABEL])))
    return error;
  node->value.ifa_addr =
      ip(node->address, info.ifa_family, attribute_data(local), info.ifa_index);
  unsigned char mask[16] = {};
  for (unsigned bit = 0; bit < info.ifa_prefixlen; ++bit)
    mask[bit / 8] |= static_cast<unsigned char>(0x80U >> (bit % 8));
  node->value.ifa_netmask = ip(node->mask, info.ifa_family, mask, 0);
  const rtattr *peer = values[IFA_BROADCAST];
  if (!peer && (node->value.ifa_flags & IFF_POINTOPOINT))
    peer = values[IFA_ADDRESS];
  if (peer)
    node->value.ifa_broadaddr =
        ip(node->peer, info.ifa_family, attribute_data(peer), info.ifa_index);
  return 0;
}

template <typename Policy>
ErrorOr<bool> parse(const unsigned char *bytes, size_t size, unsigned sequence,
                    bool links, List<Policy> &list) {
  while (size) {
    if (size < sizeof(nlmsghdr))
      return Error(EINVAL);
    auto *header = reinterpret_cast<const nlmsghdr *>(bytes);
    size_t length = header->nlmsg_len;
    if (length < sizeof(nlmsghdr) || length > size ||
        header->nlmsg_seq != sequence)
      return Error(EINVAL);
    if (header->nlmsg_flags & NLM_F_DUMP_INTR)
      return Error(EAGAIN);
    if (header->nlmsg_type == NLMSG_OVERRUN)
      return Error(ENOBUFS);
    if (header->nlmsg_type == NLMSG_DONE) {
      if (length >= NLMSG_LENGTH(sizeof(int))) {
        int status;
        copy(&status, message_data(header), sizeof(status));
        if (status)
          return Error(status < 0 ? -status : EIO);
      }
      return true;
    }
    if (header->nlmsg_type == NLMSG_ERROR) {
      if (length < NLMSG_LENGTH(sizeof(nlmsgerr)))
        return Error(EINVAL);
      int error = static_cast<const nlmsgerr *>(message_data(header))->error;
      if (error)
        return Error(error < 0 ? -error : EIO);
    } else {
      int error = 0;
      if (links && header->nlmsg_type == RTM_NEWLINK)
        error = link(header, list);
      if (!links && header->nlmsg_type == RTM_NEWADDR)
        error = address(header, list);
      if (error)
        return Error(error);
    }
    size_t step = NLMSG_ALIGN(length);
    if (step > size) {
      if (length != size)
        return Error(EINVAL);
      size = 0;
    } else {
      size -= step;
      bytes += step;
    }
  }
  return false;
}

template <typename Policy>
ErrorOr<int> dump(int fd, bool links, List<Policy> &list) {
  struct {
    nlmsghdr header;
    rtgenmsg request;
  } request = {};
  unsigned sequence = links ? 1 : 2;
  request.header.nlmsg_len = NLMSG_LENGTH(sizeof(rtgenmsg));
  request.header.nlmsg_type = links ? RTM_GETLINK : RTM_GETADDR;
  request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
  request.header.nlmsg_seq = sequence;
  request.request.rtgen_family = AF_UNSPEC;
  for (;;) {
    auto sent =
        Policy::sendto(fd, &request, request.header.nlmsg_len, 0, nullptr, 0);
    if (!sent.has_value()) {
      if (sent.error() == EINTR)
        continue;
      return Error(sent.error());
    }
    if (*sent != request.header.nlmsg_len)
      return Error(EIO);
    break;
  }
  for (;;) {
    auto length = Policy::recvfrom(fd, nullptr, 0, MSG_PEEK | MSG_TRUNC,
                                   nullptr, nullptr);
    if (!length.has_value()) {
      if (length.error() == EINTR)
        continue;
      return Error(length.error());
    }
    if (*length < static_cast<ssize_t>(sizeof(nlmsghdr)))
      return Error(EIO);
    void *storage = Policy::allocate(static_cast<size_t>(*length));
    if (!storage)
      return Error(ENOMEM);
    struct Buffer {
      void *data;
      ~Buffer() { Policy::deallocate(data); }
    } buffer{storage};
    sockaddr_nl sender = {};
    socklen_t sender_length = sizeof(sender);
    auto received =
        Policy::recvfrom(fd, storage, static_cast<size_t>(*length), MSG_TRUNC,
                         reinterpret_cast<sockaddr *>(&sender), &sender_length);
    if (!received.has_value()) {
      if (received.error() == EINTR)
        continue;
      return Error(received.error());
    }
    if (*received != *length || sender_length != sizeof(sender) ||
        sender.nl_family != AF_NETLINK || sender.nl_pid != 0)
      return Error(EIO);
    auto result = parse(static_cast<const unsigned char *>(storage),
                        static_cast<size_t>(*received), sequence, links, list);
    if (!result.has_value())
      return Error(result.error());
    if (*result)
      return 0;
  }
}

template <typename Policy> ErrorOr<ifaddrs *> enumerate() {
  auto opened =
      Policy::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
  if (!opened.has_value())
    return Error(opened.error());
  struct Socket {
    int fd;
    ~Socket() {
      if (fd >= 0)
        Policy::close(fd);
    }
  } socket{*opened};
  List<Policy> list;
  for (unsigned phase = 0; phase < 2; ++phase) {
    auto result = dump<Policy>(socket.fd, phase == 0, list);
    if (!result.has_value())
      return Error(result.error());
  }
  int fd = socket.fd;
  socket.fd = -1;
  auto closed = Policy::close(fd);
  if (!closed.has_value())
    return Error(closed.error());
  return list.release();
}

} // namespace net::ifaddrs_detail
} // namespace LIBC_NAMESPACE_DECL
#endif
