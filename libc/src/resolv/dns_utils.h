//===-- Bounded DNS wire-format operations ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_RESOLV_DNS_UTILS_H
#define LLVM_LIBC_SRC_RESOLV_DNS_UTILS_H

#include "hdr/arpa_nameser_macros.h"
#include "hdr/errno_macros.h"
#include "hdr/types/ns_sect.h"
#include "hdr/types/size_t.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace dns {

inline int fail(int error = EMSGSIZE) {
  libc_errno = error;
  return -1;
}

inline __UINT16_TYPE__ get16(const unsigned char *p) {
  return static_cast<__UINT16_TYPE__>((static_cast<unsigned int>(p[0]) << 8) |
                                      p[1]);
}

inline __UINT32_TYPE__ get32(const unsigned char *p) {
  return static_cast<__UINT32_TYPE__>((static_cast<unsigned long>(p[0]) << 24) |
                                      (static_cast<unsigned long>(p[1]) << 16) |
                                      (static_cast<unsigned long>(p[2]) << 8) |
                                      p[3]);
}

inline void put16(unsigned char *p, unsigned int value) {
  p[0] = static_cast<unsigned char>(value >> 8);
  p[1] = static_cast<unsigned char>(value);
}

inline bool valid_section(ns_sect section) {
  return static_cast<unsigned int>(section) < ns_s_max;
}

// RFC 1035 sections 3.1 and 4.1.4. A null destination validates without
// rendering. Public callers must supply pointers into one live packet array.
// The return value counts only the original encoding, not a pointer's target.
inline int expand_name(const unsigned char *msg, const unsigned char *end,
                       const unsigned char *src, char *dst, size_t size) {
  if (!msg || !end || !src || end < msg || src < msg || src >= end ||
      end - msg > NS_MAXMSG)
    return fail();
  size_t packet_size = static_cast<size_t>(end - msg);
  size_t pos = static_cast<size_t>(src - msg);
  size_t consumed = 0, written = 0, wire_size = 1;
  size_t steps = 0;
  bool jumped = false, first = true;
  auto append = [&](char c) {
    if (!dst)
      return true;
    if (written >= size)
      return false;
    dst[written++] = c;
    return true;
  };

  for (;;) {
    // Every iteration consumes a length octet or follows a pointer. No valid
    // path visits more positions than the packet contains; this bounds cycles
    // even when they contain only pointers and no label data.
    if (pos >= packet_size || ++steps > packet_size)
      return fail();
    unsigned int length = msg[pos++];
    if (!jumped)
      ++consumed;
    if ((length & NS_CMPRSFLGS) == NS_CMPRSFLGS) {
      if (pos == packet_size)
        return fail();
      size_t target = ((length & 0x3f) << 8) | msg[pos];
      if (target >= packet_size)
        return fail();
      if (!jumped)
        ++consumed;
      jumped = true;
      pos = target;
      continue;
    }
    if (length > NS_MAXLABEL)
      return fail();
    if (length == 0) {
      if ((first && !append('.')) || !append('\0'))
        return fail();
      return static_cast<int>(consumed);
    }
    if (length > packet_size - pos || wire_size + length + 1 > NS_MAXCDNAME)
      return fail();
    wire_size += length + 1;
    if (!jumped)
      consumed += length;
    if (!first && !append('.'))
      return fail();
    first = false;
    for (unsigned int i = 0; i < length; ++i) {
      unsigned char c = msg[pos++];
      if (c <= 0x20 || c >= 0x7f) {
        if (!append('\\') || !append('0' + c / 100) ||
            !append('0' + (c / 10) % 10) || !append('0' + c % 10))
          return fail();
      } else {
        if ((c == '.' || c == '\\' || c == '"' || c == ';' || c == '(' ||
             c == ')' || c == '@' || c == '$') &&
            !append('\\'))
          return fail();
        if (!append(static_cast<char>(c)))
          return fail();
      }
    }
  }
}

// Without a message base, skip APIs can validate only the encoded name, not
// compression targets. Full-message parsing uses expand_name instead.
inline int skip_name(const unsigned char *src, const unsigned char *end) {
  if (!src || !end || end < src)
    return fail();
  const unsigned char *p = src;
  size_t wire_size = 1;
  while (p != end) {
    unsigned int length = *p++;
    if ((length & NS_CMPRSFLGS) == NS_CMPRSFLGS) {
      if (p == end)
        return fail();
      return static_cast<int>(p + 1 - src);
    }
    if (length > NS_MAXLABEL)
      return fail();
    if (length == 0)
      return static_cast<int>(p - src);
    if (length > static_cast<size_t>(end - p) ||
        wire_size + length + 1 > NS_MAXCDNAME)
      return fail();
    wire_size += length + 1;
    p += length;
  }
  return fail();
}

// Advance across the fixed fields and opaque RDATA following an owner name.
inline int record_tail(const unsigned char *fields, const unsigned char *end,
                       ns_sect section) {
  size_t fixed = section == ns_s_qd ? NS_QFIXEDSZ : NS_RRFIXEDSZ;
  if (static_cast<size_t>(end - fields) < fixed)
    return fail();
  size_t data_size = section == ns_s_qd ? 0 : get16(fields + 8);
  if (data_size > static_cast<size_t>(end - fields) - fixed)
    return fail();
  return static_cast<int>(fixed + data_size);
}

// Encode presentation syntax (including escaped octets) into at most 255 bytes.
// No search suffix, IDNA conversion, resolver state, or name compression
// occurs.
inline int encode_name(const char *name, unsigned char *wire) {
  if (!name)
    return fail(EINVAL);
  const unsigned char *p = reinterpret_cast<const unsigned char *>(name);
  if (*p == 0 || (*p == '.' && p[1] == 0)) {
    wire[0] = 0;
    return 1;
  }
  size_t used = 1, label = 0;
  unsigned int length = 0;
  for (;;) {
    unsigned int c = *p++;
    if (c == 0 || c == '.') {
      if (length == 0)
        return fail(EINVAL);
      wire[label] = static_cast<unsigned char>(length);
      if (c == 0 || *p == 0) {
        wire[used++] = 0;
        return static_cast<int>(used);
      }
      // Reserve a new label length and the terminal root octet.
      if (used >= NS_MAXCDNAME - 1)
        return fail(EMSGSIZE);
      label = used++;
      length = 0;
      continue;
    }
    if (c == '\\') {
      c = *p++;
      if (c == 0)
        return fail(EINVAL);
      if (c >= '0' && c <= '9') {
        unsigned int value = c - '0';
        for (int i = 0; i < 2; ++i) {
          c = *p++;
          if (c < '0' || c > '9')
            return fail(EINVAL);
          value = value * 10 + c - '0';
        }
        if (value > 255)
          return fail(EINVAL);
        c = value;
      }
    }
    if (length == NS_MAXLABEL || used >= NS_MAXCDNAME - 1)
      return fail(EMSGSIZE);
    wire[used++] = static_cast<unsigned char>(c);
    ++length;
  }
}

} // namespace dns
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_RESOLV_DNS_UTILS_H
