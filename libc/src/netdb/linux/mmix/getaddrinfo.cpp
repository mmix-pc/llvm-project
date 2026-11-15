//===-- MMIX Linux address and service lookup
//------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/netdb/getaddrinfo.h"
#include "hdr/netinet_in_macros.h"
#include "src/__support/common.h"
#include "src/__support/net/address.h"
#include "src/net/if_nametoindex.h"
#include "src/netdb/freeaddrinfo.h"
#include "src/netdb/linux/mmix/resolver.h"
#include "src/string/memory_utils/inline_memcpy.h"
namespace LIBC_NAMESPACE_DECL {
namespace {
using namespace mmix::resolver;
int services(const char *name, int flags, int type, int protocol,
             int ports[2]) {
  ports[0] = ports[1] = -1;
  bool tcp =
      (!type || type == SOCK_STREAM) && (!protocol || protocol == IPPROTO_TCP);
  bool udp =
      (!type || type == SOCK_DGRAM) && (!protocol || protocol == IPPROTO_UDP);
  unsigned port = 0;
  bool numeric = !name || mmix::netdb::decimal(name, 65535, port);
  if (numeric) {
    if (tcp)
      ports[0] = static_cast<int>(port);
    if (udp)
      ports[1] = static_cast<int>(port);
    return 0;
  }
  if (flags & AI_NUMERICSERV)
    return EAI_NONAME;
  bool digits = *name != 0;
  for (const char *p = name; *p; p++)
    if (*p < '0' || *p > '9')
      digits = false;
  if (digits || !*name)
    return EAI_SERVICE;
  File file("/etc/services");
  while (file.next()) {
    bool match = false;
    for (size_t i = 0; i < file.count; i++)
      if (i != 1 && mmix::netdb::equal(file.tokens[i], name))
        match = true;
    if (!match)
      continue;
    char *p = file.tokens[1];
    while (*p && *p != '/')
      ++p;
    if (!*p)
      continue;
    *p++ = 0;
    if (!mmix::netdb::decimal(file.tokens[1], 65535, port))
      continue;
    if (tcp && ports[0] < 0 && mmix::netdb::equal(p, "tcp"))
      ports[0] = static_cast<int>(port);
    if (udp && ports[1] < 0 && mmix::netdb::equal(p, "udp"))
      ports[1] = static_cast<int>(port);
  }
  if (file.error)
    return file.error == ENOMEM ? EAI_MEMORY : EAI_SYSTEM;
  return ports[0] >= 0 || ports[1] >= 0 ? 0 : EAI_SERVICE;
}
int numeric(const char *name, Answers &answer, bool &matched) {
  matched = false;
  unsigned char address[16];
  if (LIBC_NAMESPACE::inet_pton(AF_INET, name, address) == 1) {
    matched = true;
    return answer.add(AF_INET, address) ? 0 : EAI_MEMORY;
  }
  // Retain the pre-existing legacy numeric IPv4 spellings as an extension.
  auto legacy = net::inet_addr(name);
  if (legacy.has_value()) {
    matched = true;
    uint32_t value = *legacy;
    return answer.add(AF_INET, &value) ? 0 : EAI_MEMORY;
  }
  char text[256];
  if (!copy(text, sizeof(text), name))
    return 0;
  char *scope = text;
  while (*scope && *scope != '%')
    ++scope;
  bool has_scope = *scope != 0;
  if (has_scope)
    *scope++ = 0;
  if (LIBC_NAMESPACE::inet_pton(AF_INET6, text, address) != 1)
    return 0;
  matched = true;
  unsigned index = 0;
  if (has_scope) {
    if (!*scope)
      return EAI_NONAME;
    if (!mmix::netdb::decimal(scope, 0xffffffffu, index)) {
      index = LIBC_NAMESPACE::if_nametoindex(scope);
      if (!index)
        return EAI_NONAME;
    }
  }
  return answer.add(AF_INET6, address, index) ? 0 : EAI_MEMORY;
}
} // namespace
LLVM_LIBC_FUNCTION(int, getaddrinfo,
                   (const char *__restrict name, const char *__restrict service,
                    const addrinfo *__restrict hints,
                    addrinfo **__restrict result)) {
  using namespace mmix::resolver;
  *result = nullptr;
  int flags = hints ? hints->ai_flags : 0,
      family = hints ? hints->ai_family : AF_UNSPEC;
  int type = hints ? hints->ai_socktype : 0,
      protocol = hints ? hints->ai_protocol : 0;
  if (flags & ~(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST | AI_NUMERICSERV |
                AI_V4MAPPED | AI_ALL | AI_ADDRCONFIG))
    return EAI_BADFLAGS;
  if (family != AF_INET && family != AF_INET6 && family != AF_UNSPEC)
    return EAI_FAMILY;
  if (type && type != SOCK_STREAM && type != SOCK_DGRAM)
    return EAI_SOCKTYPE;
  if ((protocol && protocol != IPPROTO_TCP && protocol != IPPROTO_UDP) ||
      (type == SOCK_STREAM && protocol == IPPROTO_UDP) ||
      (type == SOCK_DGRAM && protocol == IPPROTO_TCP))
    return EAI_SERVICE;
  if (!name && !service)
    return EAI_NONAME;
  if (!name && (flags & AI_CANONNAME))
    return EAI_BADFLAGS;
  int ports[2];
  int error = services(service, flags, type, protocol, ports);
  if (error)
    return error;
  bool v4 = true, v6 = true;
  if (flags & AI_ADDRCONFIG) {
    error = configured_families(v4, v6);
    if (error)
      return error;
  }
  bool mapped = family == AF_INET6 && (flags & AI_V4MAPPED);
  bool want4 = v4 && (family != AF_INET6 || mapped),
       want6 = v6 && family != AF_INET;
  if (!want4 && !want6)
    return EAI_NONAME;
  Answers addresses;
  if (!name) {
    unsigned char a4[4] = {}, a6[16] = {};
    if (!(flags & AI_PASSIVE)) {
      a4[0] = 127;
      a4[3] = 1;
      a6[15] = 1;
    }
    if (want4 && !addresses.add(AF_INET, a4))
      return EAI_MEMORY;
    if (want6 && !addresses.add(AF_INET6, a6))
      return EAI_MEMORY;
  } else {
    bool matched = false;
    error = numeric(name, addresses, matched);
    if (error)
      return error;
    if (!matched) {
      if (flags & AI_NUMERICHOST)
        return EAI_NONAME;
      error = local_addresses(name, addresses, matched);
      if (error)
        return error;
      if (!matched) {
        error = dns_lookup(name, want4, want6, addresses,
                           mapped && !(flags & AI_ALL));
        if (error)
          return error;
      }
    }
  }
  bool have6 = false;
  for (Address *p = addresses.head; p; p = p->next)
    if (p->family == AF_INET6 && want6)
      have6 = true;
  addrinfo **tail = result;
  struct Node {
    addrinfo info;
    sockaddr_storage address;
  };
  const char *canonical = addresses.canonical[0] ? addresses.canonical : name;
  for (Address *p = addresses.head; p; p = p->next) {
    if ((p->family == AF_INET && !want4) || (p->family == AF_INET6 && !want6))
      continue;
    if (p->family == AF_INET && mapped && have6 && !(flags & AI_ALL))
      continue;
    for (unsigned transport = 0; transport < 2; transport++) {
      if (ports[transport] < 0)
        continue;
      size_t canon_size =
          !*result && (flags & AI_CANONNAME) ? length(canonical) + 1 : 0;
      if (canon_size > static_cast<size_t>(-1) - sizeof(Node)) {
        error = EAI_MEMORY;
        break;
      }
      auto *node = static_cast<Node *>(::malloc(sizeof(Node) + canon_size));
      if (!node) {
        error = EAI_MEMORY;
        break;
      }
      *node = {};
      node->info.ai_socktype = transport ? SOCK_DGRAM : SOCK_STREAM;
      node->info.ai_protocol = transport ? IPPROTO_UDP : IPPROTO_TCP;
      node->info.ai_family =
          p->family == AF_INET && mapped ? AF_INET6 : p->family;
      node->info.ai_addr = reinterpret_cast<sockaddr *>(&node->address);
      if (node->info.ai_family == AF_INET) {
        auto *a = reinterpret_cast<sockaddr_in *>(&node->address);
        a->sin_family = AF_INET;
        a->sin_port =
            Endian::to_big_endian(static_cast<uint16_t>(ports[transport]));
        inline_memcpy(&a->sin_addr, p->bytes, 4);
        node->info.ai_addrlen = sizeof(*a);
      } else {
        auto *a = reinterpret_cast<sockaddr_in6 *>(&node->address);
        a->sin6_family = AF_INET6;
        a->sin6_port =
            Endian::to_big_endian(static_cast<uint16_t>(ports[transport]));
        a->sin6_scope_id = p->scope;
        if (p->family == AF_INET) {
          a->sin6_addr.s6_addr[10] = a->sin6_addr.s6_addr[11] = 255;
          inline_memcpy(a->sin6_addr.s6_addr + 12, p->bytes, 4);
        } else
          inline_memcpy(&a->sin6_addr, p->bytes, 16);
        node->info.ai_addrlen = sizeof(*a);
      }
      if (canon_size) {
        node->info.ai_canonname = reinterpret_cast<char *>(node + 1);
        copy(node->info.ai_canonname, canon_size, canonical);
      }
      *tail = &node->info;
      tail = &node->info.ai_next;
    }
    if (error)
      break;
  }
  if (error) {
    LIBC_NAMESPACE::freeaddrinfo(*result);
    *result = nullptr;
    return error;
  }
  return *result ? 0 : EAI_NONAME;
}
} // namespace LIBC_NAMESPACE_DECL
