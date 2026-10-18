//===-- Implementation of inet_pton
//----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/arpa/inet/inet_pton.h"
#include "hdr/errno_macros.h"
#include "hdr/sys_socket_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
namespace {

bool parse_ipv4(const char *src, unsigned char *dst) {
  for (unsigned i = 0; i < 4; ++i) {
    const char *start = src;
    unsigned value = 0;
    while (*src >= '0' && *src <= '9') {
      value = value * 10 + (*src++ - '0');
      if (value > 255 || (src - start > 1 && *start == '0'))
        return false;
    }
    if (src == start)
      return false;
    dst[i] = static_cast<unsigned char>(value);
    if (i == 3)
      return *src == '\0';
    if (*src++ != '.')
      return false;
  }
  return false;
}

int hex_digit(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

bool parse_ipv6(const char *src, unsigned char *dst) {
  unsigned used = 0;
  int compressed = -1;
  if (*src == ':') {
    if (*++src != ':')
      return false;
    ++src;
    compressed = 0;
  }
  while (*src) {
    if (used == 16)
      return false;
    const char *start = src;
    unsigned value = 0;
    int digit;
    while ((digit = hex_digit(*src)) >= 0) {
      if (src - start == 4)
        return false;
      value = value * 16 + static_cast<unsigned>(digit);
      ++src;
    }
    if (*src == '.') {
      if (used > 12 || !parse_ipv4(start, dst + used))
        return false;
      used += 4;
      break;
    }
    if (src == start)
      return false;
    dst[used++] = static_cast<unsigned char>(value >> 8);
    dst[used++] = static_cast<unsigned char>(value);
    if (!*src)
      break;
    if (*src++ != ':')
      return false;
    if (*src == ':') {
      if (compressed >= 0)
        return false;
      compressed = static_cast<int>(used);
      ++src;
    } else if (!*src) {
      return false;
    }
  }
  if (compressed < 0)
    return used == 16;
  if (used == 16)
    return false;

  // Move the suffix backwards before filling the omitted network-order words.
  unsigned gap = 16 - used;
  for (unsigned i = used; i > static_cast<unsigned>(compressed); --i)
    dst[i - 1 + gap] = dst[i - 1];
  for (unsigned i = compressed; i < compressed + gap; ++i)
    dst[i] = 0;
  return true;
}

} // namespace

LLVM_LIBC_FUNCTION(int, inet_pton,
                   (int af, const char *__restrict src, void *__restrict dst)) {
  if (af != AF_INET && af != AF_INET6) {
    libc_errno = EAFNOSUPPORT;
    return -1;
  }
  // Invalid input must leave the caller's destination unchanged.
  unsigned char bytes[16] = {};
  if (!(af == AF_INET ? parse_ipv4(src, bytes) : parse_ipv6(src, bytes)))
    return 0;
  for (unsigned i = 0, size = af == AF_INET ? 4 : 16; i < size; ++i)
    static_cast<unsigned char *>(dst)[i] = bytes[i];
  return 1;
}

} // namespace LIBC_NAMESPACE_DECL
