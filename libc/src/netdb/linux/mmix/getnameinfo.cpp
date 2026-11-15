//===-- MMIX Linux reverse address and service lookup ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/netdb/getnameinfo.h"
#include "hdr/net_if_macros.h"
#include "src/__support/common.h"
#include "src/arpa/inet/inet_ntop.h"
#include "src/net/if_indextoname.h"
#include "src/netdb/linux/mmix/resolver.h"
#include "src/stdio/snprintf.h"
#include "src/unistd/gethostname.h"
namespace LIBC_NAMESPACE_DECL {
namespace {
using namespace mmix::resolver;
int shorten(char name[256]) {
  char local[256] = {};
  File file("/etc/hosts");
  while (file.next()) {
    bool match = false;
    for (size_t i = 1; i < file.count; i++)
      if (same(file.tokens[i], "localhost"))
        match = true;
    if (!match)
      continue;
    unsigned char bytes[16];
    if (LIBC_NAMESPACE::inet_pton(AF_INET, file.tokens[0], bytes) != 1 &&
        LIBC_NAMESPACE::inet_pton(AF_INET6, file.tokens[0], bytes) != 1)
      continue;
    if (!copy(local, sizeof(local), file.tokens[1]))
      return EAI_FAIL;
    break;
  }
  if (file.error)
    return file.error == ENOMEM ? EAI_MEMORY : EAI_SYSTEM;
  char *suffix = local;
  while (*suffix && *suffix != '.')
    ++suffix;
  if (!*suffix) {
    if (LIBC_NAMESPACE::gethostname(local, sizeof(local)))
      return EAI_SYSTEM;
    suffix = local;
    while (*suffix && *suffix != '.')
      ++suffix;
  }
  if (!*suffix || !suffix[1])
    return 0;
  char *dot = name;
  while (*dot && *dot != '.')
    ++dot;
  if (*dot && same(dot + 1, suffix + 1))
    *dot = 0;
  return 0;
}
} // namespace
LLVM_LIBC_FUNCTION(int, getnameinfo,
                   (const sockaddr *address, socklen_t address_size, char *host,
                    socklen_t host_size, char *service, socklen_t service_size,
                    int flags)) {
  using namespace mmix::resolver;
  if (flags & ~(NI_NUMERICHOST | NI_NUMERICSERV | NI_NOFQDN | NI_NAMEREQD |
                NI_DGRAM | NI_NUMERICSCOPE))
    return EAI_BADFLAGS;
  if (!address || address_size < sizeof(address->sa_family))
    return EAI_FAMILY;
  int family = address->sa_family;
  if ((family != AF_INET && family != AF_INET6) ||
      (family == AF_INET && address_size < sizeof(sockaddr_in)) ||
      (family == AF_INET6 && address_size < sizeof(sockaddr_in6)))
    return EAI_FAMILY;
  bool want_host = host && host_size, want_service = service && service_size;
  if (!want_host && !want_service)
    return EAI_NONAME;
  const unsigned char *bytes;
  uint16_t port;
  uint32_t scope = 0;
  if (family == AF_INET) {
    const auto *a = reinterpret_cast<const sockaddr_in *>(address);
    bytes = reinterpret_cast<const unsigned char *>(&a->sin_addr);
    port = Endian::from_big_endian(a->sin_port);
  } else {
    const auto *a = reinterpret_cast<const sockaddr_in6 *>(address);
    bytes = a->sin6_addr.s6_addr;
    port = Endian::from_big_endian(a->sin6_port);
    scope = a->sin6_scope_id;
  }
  char host_text[256] = {}, service_number[6];
  const char *service_text = service_number;
  File services("/etc/services");
  if (want_service) {
    LIBC_NAMESPACE::snprintf(service_number, sizeof(service_number), "%u",
                             static_cast<unsigned>(port));
    if (!(flags & NI_NUMERICSERV)) {
      while (services.next()) {
        char *p = services.tokens[1];
        while (*p && *p != '/')
          ++p;
        if (!*p)
          continue;
        *p++ = 0;
        unsigned value;
        if (!mmix::netdb::decimal(services.tokens[1], 65535, value))
          continue;
        if (value == port &&
            mmix::netdb::equal(p, (flags & NI_DGRAM) ? "udp" : "tcp")) {
          service_text = services.tokens[0];
          break;
        }
      }
      if (services.error)
        return services.error == ENOMEM ? EAI_MEMORY : EAI_SYSTEM;
    }
  }
  if (want_host) {
    bool matched = false;
    if (!(flags & NI_NUMERICHOST)) {
      int lookup_family = family;
      const unsigned char *lookup_bytes = bytes;
      unsigned char zero[16] = {};
      bool unspecified = family == AF_INET6 && bytes_equal(bytes, zero, 16);
      if (family == AF_INET6 && bytes_equal(bytes, zero, 10) &&
          ((bytes[10] == 255 && bytes[11] == 255) ||
           (!bytes[10] && !bytes[11] &&
            (bytes[12] || bytes[13] || bytes[14] || bytes[15] > 1)))) {
        lookup_family = AF_INET;
        lookup_bytes = bytes + 12;
      }
      if (!unspecified) {
        int error =
            local_reverse(lookup_family, lookup_bytes, host_text, matched);
        if (error)
          return error;
        if (!matched) {
          char reverse[80];
          if (lookup_family == AF_INET)
            LIBC_NAMESPACE::snprintf(reverse, sizeof(reverse),
                                     "%u.%u.%u.%u.in-addr.arpa",
                                     static_cast<unsigned>(lookup_bytes[3]),
                                     static_cast<unsigned>(lookup_bytes[2]),
                                     static_cast<unsigned>(lookup_bytes[1]),
                                     static_cast<unsigned>(lookup_bytes[0]));
          else {
            const char *hex = "0123456789abcdef";
            size_t at = 0;
            for (int i = 15; i >= 0; i--) {
              reverse[at++] = hex[lookup_bytes[i] & 15];
              reverse[at++] = '.';
              reverse[at++] = hex[lookup_bytes[i] >> 4];
              reverse[at++] = '.';
            }
            copy(reverse + at, sizeof(reverse) - at, "ip6.arpa");
          }
          error = dns_reverse(reverse, host_text);
          if (error && error != EAI_NONAME)
            return error;
          matched = !error;
        }
      }
      if (matched && (flags & NI_NOFQDN)) {
        int error = shorten(host_text);
        if (error)
          return error;
      }
    }
    if (!matched) {
      if (flags & NI_NAMEREQD)
        return EAI_NONAME;
      if (!LIBC_NAMESPACE::inet_ntop(family, bytes, host_text,
                                     sizeof(host_text)))
        return EAI_FAIL;
      if (family == AF_INET6 && scope) {
        size_t n = length(host_text);
        host_text[n++] = '%';
        char name[IF_NAMESIZE];
        if (!(flags & NI_NUMERICSCOPE) &&
            LIBC_NAMESPACE::if_indextoname(scope, name))
          copy(host_text + n, sizeof(host_text) - n, name);
        else
          LIBC_NAMESPACE::snprintf(host_text + n, sizeof(host_text) - n, "%u",
                                   scope);
      }
    }
  }
  if ((want_host && length(host_text) >= host_size) ||
      (want_service && length(service_text) >= service_size))
    return EAI_OVERFLOW;
  if (want_host)
    copy(host, host_size, host_text);
  if (want_service)
    copy(service, service_size, service_text);
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
