//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Implementation of inet_ntop function.
///
//===----------------------------------------------------------------------===//

#include "src/arpa/inet/inet_ntop.h"
#include "hdr/errno_macros.h"
#include "hdr/sys_socket_macros.h"
#include "hdr/types/struct_in6_addr.h"
#include "hdr/types/struct_in_addr.h"
#include "src/__support/CPP/span.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/null_check.h"
#include "src/__support/net/address.h"
#include "src/string/memory_utils/inline_memcpy.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(const char *, inet_ntop,
                   (int af, const void *__restrict src, char *__restrict dst,
                    socklen_t size)) {
  LIBC_CRASH_ON_NULLPTR(src);
  LIBC_CRASH_ON_NULLPTR(dst);

  bool success;
  if (af == AF_INET) {
    struct in_addr address;
    inline_memcpy(&address, src, sizeof(address));
    success = net::ipv4_to_str(address, cpp::span<char>(dst, size));
  } else if (af == AF_INET6) {
    struct in6_addr address;
    inline_memcpy(&address, src, sizeof(address));
    success = net::ipv6_to_str(address, cpp::span<char>(dst, size));
  } else {
    libc_errno = EAFNOSUPPORT;
    return nullptr;
  }

  if (!success) {
    libc_errno = ENOSPC;
    return nullptr;
  }

  return dst;
}

} // namespace LIBC_NAMESPACE_DECL
