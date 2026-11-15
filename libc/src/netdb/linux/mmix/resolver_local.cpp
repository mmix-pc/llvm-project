//===-- MMIX Linux per-call local resolver sources ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/netdb/linux/mmix/resolver.h"
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
namespace LIBC_NAMESPACE_DECL {
namespace mmix::resolver {
int local_addresses(const char *name, Answers &answer, bool &matched) {
  matched = false;
  File file("/etc/hosts");
  while (file.next()) {
    bool match = false;
    for (size_t i = 1; i < file.count; i++)
      if (same(file.tokens[i], name))
        match = true;
    if (!match)
      continue;
    unsigned char address[16];
    int family = AF_INET;
    if (LIBC_NAMESPACE::inet_pton(AF_INET, file.tokens[0], address) != 1) {
      if (LIBC_NAMESPACE::inet_pton(AF_INET6, file.tokens[0], address) != 1)
        continue;
      family = AF_INET6;
    }
    matched = true;
    if (!answer.add(family, address))
      return EAI_MEMORY;
    if (!answer.canonical[0] &&
        !copy(answer.canonical, sizeof(answer.canonical), file.tokens[1]))
      return EAI_FAIL;
  }
  if (file.error)
    return file.error == ENOMEM ? EAI_MEMORY : EAI_SYSTEM;
  return 0;
}
int local_reverse(int family, const void *address, char output[256],
                  bool &matched) {
  matched = false;
  File file("/etc/hosts");
  while (file.next()) {
    unsigned char value[16];
    if (LIBC_NAMESPACE::inet_pton(family, file.tokens[0], value) != 1 ||
        !bytes_equal(address, value, family == AF_INET ? 4 : 16))
      continue;
    if (!copy(output, 256, file.tokens[1]))
      return EAI_FAIL;
    matched = true;
    return 0;
  }
  if (file.error)
    return file.error == ENOMEM ? EAI_MEMORY : EAI_SYSTEM;
  return 0;
}
int configured_families(bool &ipv4, bool &ipv6) {
  ipv4 = ipv6 = false;
  long result = syscall_impl<long>(SYS_socket, AF_NETLINK,
                                   SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return EAI_SYSTEM;
  }
  struct Owner {
    int fd;
    ~Owner() { syscall_impl<long>(SYS_close, fd); }
  } owner{static_cast<int>(result)};
  struct {
    nlmsghdr header;
    ifaddrmsg address;
  } request{};
  request.header.nlmsg_len = sizeof(request);
  request.header.nlmsg_type = RTM_GETADDR;
  request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
  request.address.ifa_family = AF_UNSPEC;
  result = syscall_impl<long>(SYS_sendto, owner.fd, &request, sizeof(request),
                              0, nullptr, 0);
  if (result < 0) {
    libc_errno = static_cast<int>(-result);
    return EAI_SYSTEM;
  }
  alignas(nlmsghdr) unsigned char buffer[8192];
  for (;;) {
    result = syscall_impl<long>(SYS_recvfrom, owner.fd, buffer, sizeof(buffer),
                                MSG_TRUNC, nullptr, nullptr);
    if (result == -EINTR)
      continue;
    if (result < 0) {
      libc_errno = static_cast<int>(-result);
      return EAI_SYSTEM;
    }
    if (!result || result > static_cast<long>(sizeof(buffer))) {
      libc_errno = ENOBUFS;
      return EAI_SYSTEM;
    }
    size_t left = static_cast<size_t>(result);
    for (auto *h = reinterpret_cast<nlmsghdr *>(buffer); NLMSG_OK(h, left);
         h = NLMSG_NEXT(h, left)) {
      if (h->nlmsg_flags & NLM_F_DUMP_INTR)
        return EAI_AGAIN;
      if (h->nlmsg_type == NLMSG_DONE)
        return 0;
      if (h->nlmsg_type == NLMSG_ERROR) {
        libc_errno = EIO;
        return EAI_SYSTEM;
      }
      if (h->nlmsg_type != RTM_NEWADDR ||
          h->nlmsg_len < NLMSG_LENGTH(sizeof(ifaddrmsg)))
        continue;
      auto *address = static_cast<ifaddrmsg *>(NLMSG_DATA(h));
      if (address->ifa_flags & IFA_F_TENTATIVE)
        continue;
      if (address->ifa_family == AF_INET)
        ipv4 = true;
      if (address->ifa_family == AF_INET6)
        ipv6 = true;
    }
    if (left) {
      libc_errno = EIO;
      return EAI_SYSTEM;
    }
  }
}
} // namespace mmix::resolver
} // namespace LIBC_NAMESPACE_DECL
