//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Implementation of getaddrinfo.
///
//===----------------------------------------------------------------------===//

#include "src/netdb/getaddrinfo.h"
#include "hdr/errno_macros.h"
#include "hdr/func/malloc.h"
#include "hdr/netdb_macros.h"
#include "hdr/netinet_in_macros.h"
#include "hdr/sys_socket_macros.h"
#include "hdr/types/struct_addrinfo.h"
#include "hdr/types/struct_sockaddr_in.h"
#include "src/__support/common.h"
#include "src/__support/endian_internal.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "src/__support/net/address.h"
#include "src/arpa/inet/inet_ntop.h"
#include "src/netdb/freeaddrinfo.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, getaddrinfo,
                   (const char *__restrict nodename,
                    const char *__restrict servname,
                    const struct addrinfo *__restrict hints,
                    struct addrinfo **__restrict res)) {
  *res = nullptr;
  const int flags = hints ? hints->ai_flags : 0;
  const int family = hints ? hints->ai_family : AF_UNSPEC;
  const int type = hints ? hints->ai_socktype : 0;
  const int protocol = hints ? hints->ai_protocol : 0;
  if (flags & ~(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST | AI_NUMERICSERV))
    return EAI_BADFLAGS;
  // FIXME: Add IPv6 resolution independently of the IPv4 numeric provider.
  if (family != AF_UNSPEC && family != AF_INET)
    return EAI_FAMILY;
  if (type != 0 && type != SOCK_STREAM && type != SOCK_DGRAM)
    return EAI_SOCKTYPE;
  if ((protocol != 0 && protocol != IPPROTO_TCP && protocol != IPPROTO_UDP) ||
      (type == SOCK_STREAM && protocol == IPPROTO_UDP) ||
      (type == SOCK_DGRAM && protocol == IPPROTO_TCP))
    return EAI_SERVICE;
  if (!nodename && !servname)
    return EAI_NONAME;
  if (!nodename && (flags & AI_CANONNAME))
    return EAI_BADFLAGS;

  unsigned port = 0;
  if (servname) {
    if (!*servname)
      return EAI_SERVICE;
    for (const char *p = servname; *p; ++p) {
      if (*p < '0' || *p > '9') {
        if (flags & AI_NUMERICSERV)
          return EAI_NONAME;
        // FIXME: Add service database lookup independently of numeric ports.
        libc_errno = ENOSYS;
        return EAI_SYSTEM;
      }
      port = port * 10 + static_cast<unsigned>(*p - '0');
      if (port > 65535)
        return EAI_SERVICE;
    }
  }
  in_addr address{};
  if (nodename) {
    auto parsed = net::inet_addr(nodename);
    if (!parsed.has_value()) {
      if (flags & AI_NUMERICHOST)
        return EAI_NONAME;
      // FIXME: Add hosts-file and DNS lookup; never invent a resolved address.
      libc_errno = ENOSYS;
      return EAI_SYSTEM;
    }
    address.s_addr = *parsed;
  } else {
    address.s_addr =
        Endian::to_big_endian(uint32_t((flags & AI_PASSIVE) ? 0 : 0x7f000001));
  }

  struct Result {
    addrinfo info;
    sockaddr_in address;
    char canonical_name[16];
  };
  addrinfo **tail = res;
  const int socket_types[] = {SOCK_STREAM, SOCK_DGRAM};
  for (int socket_type : socket_types) {
    int socket_protocol =
        socket_type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP;
    if ((type && type != socket_type) ||
        (protocol && protocol != socket_protocol))
      continue;
    auto *entry = static_cast<Result *>(::malloc(sizeof(Result)));
    if (!entry) {
      LIBC_NAMESPACE::freeaddrinfo(*res);
      *res = nullptr;
      return EAI_MEMORY;
    }
    *entry = {};
    entry->address.sin_family = AF_INET;
    entry->address.sin_port =
        Endian::to_big_endian(static_cast<uint16_t>(port));
    entry->address.sin_addr = address;
    entry->info.ai_flags = flags;
    entry->info.ai_family = AF_INET;
    entry->info.ai_socktype = socket_type;
    entry->info.ai_protocol = socket_protocol;
    entry->info.ai_addrlen = sizeof(sockaddr_in);
    entry->info.ai_addr = reinterpret_cast<sockaddr *>(&entry->address);
    if (!*res && (flags & AI_CANONNAME)) {
      LIBC_NAMESPACE::inet_ntop(AF_INET, &address, entry->canonical_name,
                                sizeof(entry->canonical_name));
      entry->info.ai_canonname = entry->canonical_name;
    }
    *tail = &entry->info;
    tail = &entry->info.ai_next;
  }
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
