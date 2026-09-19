//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/netdb/getnameinfo.h"
#include "hdr/errno_macros.h"
#include "hdr/netdb_macros.h"
#include "hdr/sys_socket_macros.h"
#include "hdr/types/struct_sockaddr_in.h"
#include "src/__support/common.h"
#include "src/__support/endian_internal.h"
#include "src/__support/libc_errno.h"
#include "src/arpa/inet/inet_ntop.h"
#include "src/stdio/snprintf.h"
#include "src/string/string_utils.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, getnameinfo,
                   (const sockaddr *address, socklen_t length, char *host,
                    socklen_t host_length, char *service,
                    socklen_t service_length, int flags)) {
  if (flags &
      ~(NI_NUMERICHOST | NI_NUMERICSERV | NI_NAMEREQD | NI_DGRAM | NI_NOFQDN))
    return EAI_BADFLAGS;
  if (!address || length < sizeof(sockaddr_in) || address->sa_family != AF_INET)
    return EAI_FAMILY;
  bool want_host = host && host_length;
  bool want_service = service && service_length;
  if (!want_host && !want_service)
    return EAI_NONAME;
  // FIXME: Add reverse-name and service database lookup. Numeric-only callers
  // must not require a resolver, and name-requiring callers must not succeed.
  if ((want_host && !(flags & NI_NUMERICHOST)) ||
      (want_service && !(flags & NI_NUMERICSERV))) {
    libc_errno = ENOSYS;
    return EAI_SYSTEM;
  }
  const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(address);
  char host_text[16];
  char service_text[6];
  LIBC_NAMESPACE::inet_ntop(AF_INET, &ipv4->sin_addr, host_text,
                            sizeof(host_text));
  int service_size = LIBC_NAMESPACE::snprintf(
      service_text, sizeof(service_text), "%u",
      static_cast<unsigned>(Endian::from_big_endian(ipv4->sin_port)));
  size_t host_size = internal::string_length(host_text);
  if ((want_host && host_size >= host_length) ||
      (want_service && static_cast<unsigned>(service_size) >= service_length))
    return EAI_OVERFLOW;
  if (want_host)
    for (size_t i = 0; i <= host_size; ++i)
      host[i] = host_text[i];
  if (want_service)
    for (int i = 0; i <= service_size; ++i)
      service[i] = service_text[i];
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
