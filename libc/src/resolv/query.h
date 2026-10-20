//===-- DNS query construction ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_RESOLV_QUERY_H
#define LLVM_LIBC_SRC_RESOLV_QUERY_H

#include "hdr/types/ns_opcode.h"
#include "src/resolv/dns_utils.h"

namespace LIBC_NAMESPACE_DECL {
namespace dns {

// Reader has the internal getrandom signature and returns ErrorOr<ssize_t>.
// This is a test boundary, not a public hook or a production entropy fallback.
template <typename Reader>
int make_query(int op, const char *name, int rr_class, int type,
               const unsigned char *data, int datalen,
               const unsigned char *newrr, unsigned char *buffer, int buflen,
               Reader &&random) {
  if (op != ns_o_query || !name || !buffer || rr_class < 0 ||
      rr_class > 65535 || type < 0 || type > 65535 || data || datalen != 0 ||
      newrr)
    return fail(EINVAL);
  if (buflen < NS_HFIXEDSZ + NS_QFIXEDSZ + 1)
    return fail();

  unsigned char packet[NS_HFIXEDSZ + NS_MAXCDNAME + NS_QFIXEDSZ] = {};
  int name_size = encode_name(name, packet + NS_HFIXEDSZ);
  if (name_size < 0)
    return -1;
  int total = NS_HFIXEDSZ + name_size + NS_QFIXEDSZ;
  if (total > buflen)
    return fail();

  packet[2] = 1; // RD, with QR/opcode/AA/TC clear.
  put16(packet + 4, 1);
  put16(packet + NS_HFIXEDSZ + name_size, static_cast<unsigned int>(type));
  put16(packet + NS_HFIXEDSZ + name_size + 2,
        static_cast<unsigned int>(rr_class));

  const int saved_errno = libc_errno;
  size_t filled = 0;
  while (filled < NS_INT16SZ) {
    auto result = random(packet + filled, NS_INT16SZ - filled, 0);
    if (!result.has_value()) {
      if (result.error() == EINTR)
        continue;
      return fail(result.error());
    }
    if (result.value() <= 0 ||
        static_cast<size_t>(result.value()) > NS_INT16SZ - filled)
      return fail(EIO);
    filled += static_cast<size_t>(result.value());
  }
  for (int i = 0; i < total; ++i)
    buffer[i] = packet[i];
  libc_errno = saved_errno;
  return total;
}

} // namespace dns
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_RESOLV_QUERY_H
