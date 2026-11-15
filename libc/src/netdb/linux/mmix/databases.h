//===-- MMIX Linux local network databases -----------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC_NETDB_LINUX_MMIX_DATABASES_H
#define LLVM_LIBC_SRC_NETDB_LINUX_MMIX_DATABASES_H
#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/func/free.h"
#include "hdr/func/realloc.h"
#include "hdr/sys_socket_macros.h"
#include "hdr/types/struct_hostent.h"
#include "hdr/types/struct_netent.h"
#include "hdr/types/struct_protoent.h"
#include "hdr/types/struct_servent.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/endian_internal.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "src/arpa/inet/inet_pton.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix::netdb {
inline bool equal(const char *a, const char *b) {
  while (*a && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}
inline bool space(char c) { return c == ' ' || c == '\t' || c == '\r'; }
inline bool decimal(const char *p, unsigned limit, unsigned &value) {
  if (!*p)
    return false;
  value = 0;
  for (; *p; ++p) {
    if (*p < '0' || *p > '9')
      return false;
    unsigned digit = *p - '0';
    if (value > (limit - digit) / 10)
      return false;
    value = value * 10 + digit;
  }
  return true;
}
// Legacy enumeration functions share borrowed storage and require callers to
// serialize use. Keep their state separate from reentrant resolver results.
struct Database {
  const char *path;
  int fd = -1;
  bool stay = false;
  int error = 0;
  char buffer[512] = {};
  size_t at = 0, available = 0;
  char *line = nullptr;
  size_t line_capacity = 0;
  char **tokens = nullptr;
  size_t token_capacity = 0, count = 0;

  void close_file() {
    if (fd >= 0)
      syscall_impl<long>(SYS_close, fd);
    fd = -1;
    at = available = 0;
  }
  void end() {
    close_file();
    ::free(line);
    ::free(tokens);
    line = nullptr;
    tokens = nullptr;
    line_capacity = token_capacity = count = 0;
    stay = false;
  }
  void rewind(bool keep) {
    stay = keep;
    close_file();
  }
  bool open() {
    if (fd >= 0)
      return true;
    long result =
        syscall_impl<long>(SYS_openat, AT_FDCWD, path, O_RDONLY | O_CLOEXEC, 0);
    if (result < 0) {
      libc_errno = static_cast<int>(-result);
      return false;
    }
    fd = static_cast<int>(result);
    return true;
  }
  bool grow_line(size_t need) {
    if (need <= line_capacity)
      return true;
    size_t capacity = line_capacity ? line_capacity : 128;
    while (capacity < need) {
      if (capacity > static_cast<size_t>(-1) / 2) {
        libc_errno = ENOMEM;
        return false;
      }
      capacity *= 2;
    }
    void *p = ::realloc(line, capacity);
    if (!p) {
      libc_errno = ENOMEM;
      return false;
    }
    line = static_cast<char *>(p);
    line_capacity = capacity;
    return true;
  }
  int read_line() {
    if (!open())
      return -1;
    size_t length = 0;
    for (;;) {
      if (at == available) {
        long n;
        do {
          n = syscall_impl<long>(SYS_read, fd, buffer, sizeof(buffer));
        } while (n == -EINTR);
        if (n < 0) {
          libc_errno = static_cast<int>(-n);
          return -1;
        }
        if (n == 0) {
          if (!length)
            return 0;
          break;
        }
        at = 0;
        available = static_cast<size_t>(n);
      }
      char c = buffer[at++];
      if (c == '\n')
        break;
      if (length > static_cast<size_t>(-1) - 2) {
        libc_errno = ENOMEM;
        return -1;
      }
      if (!grow_line(length + 2))
        return -1;
      line[length++] = c;
    }
    if (!grow_line(length + 1))
      return -1;
    line[length] = 0;
    return 1;
  }
  bool split() {
    count = 0;
    char *p = line;
    while (*p && *p != '#') {
      while (space(*p))
        ++p;
      if (!*p || *p == '#')
        break;
      if (count + 1 >= token_capacity) {
        size_t capacity = token_capacity ? token_capacity * 2 : 8;
        if (capacity < token_capacity ||
            capacity > static_cast<size_t>(-1) / sizeof(char *)) {
          libc_errno = ENOMEM;
          return false;
        }
        void *q = ::realloc(tokens, capacity * sizeof(char *));
        if (!q) {
          libc_errno = ENOMEM;
          return false;
        }
        tokens = static_cast<char **>(q);
        token_capacity = capacity;
      }
      tokens[count++] = p;
      while (*p && !space(*p) && *p != '#')
        ++p;
      bool comment = *p == '#';
      if (*p)
        *p++ = 0;
      if (comment)
        break;
    }
    if (tokens)
      tokens[count] = nullptr;
    return true;
  }
  bool next() {
    error = 0;
    for (;;) {
      int result = read_line();
      if (result < 0) {
        error = libc_errno;
        return false;
      }
      if (!result)
        return false;
      if (!split()) {
        error = libc_errno;
        return false;
      }
      if (count >= 2)
        return true;
    }
  }
};
inline Database hosts{"/etc/hosts"}, networks{"/etc/networks"},
    protocols{"/etc/protocols"}, services{"/etc/services"};
inline hostent host_record{};
inline netent net_record{};
inline protoent proto_record{};
inline servent service_record{};
inline unsigned char host_address[16];
inline char *host_addresses[2] = {reinterpret_cast<char *>(host_address),
                                  nullptr};

inline hostent *next_host() {
  while (hosts.next()) {
    int family = AF_INET, length = 4;
    if (LIBC_NAMESPACE::inet_pton(AF_INET, hosts.tokens[0], host_address) !=
        1) {
      if (LIBC_NAMESPACE::inet_pton(AF_INET6, hosts.tokens[0], host_address) !=
          1)
        continue;
      family = AF_INET6;
      length = 16;
    }
    host_record = {hosts.tokens[1], hosts.tokens + 2, family, length,
                   host_addresses};
    return &host_record;
  }
  return nullptr;
}
inline bool network_number(const char *p, uint32_t &number) {
  number = 0;
  for (unsigned parts = 0; parts < 4; ++parts) {
    unsigned base = 10, value = 0, digits = 0;
    if (*p == '0') {
      base = 8;
      ++p;
      digits = 1;
      if (*p == 'x' || *p == 'X') {
        base = 16;
        ++p;
        digits = 0;
      }
    }
    while (*p && *p != '.') {
      unsigned d = *p >= '0' && *p <= '9'   ? *p - '0'
                   : *p >= 'a' && *p <= 'f' ? *p - 'a' + 10
                   : *p >= 'A' && *p <= 'F' ? *p - 'A' + 10
                                            : 16;
      if (d >= base || value > (255 - d) / base)
        return false;
      value = value * base + d;
      ++p;
      ++digits;
    }
    if (!digits)
      return false;
    number = (number << 8) | value;
    if (!*p)
      return true;
    ++p;
  }
  return false;
}
inline netent *next_net() {
  while (networks.next()) {
    uint32_t number;
    if (!network_number(networks.tokens[1], number))
      continue;
    net_record = {networks.tokens[0], networks.tokens + 2, AF_INET, number};
    return &net_record;
  }
  return nullptr;
}
inline protoent *next_proto() {
  while (protocols.next()) {
    unsigned number;
    if (!decimal(protocols.tokens[1], 0x7fffffff, number))
      continue;
    proto_record = {protocols.tokens[0], protocols.tokens + 2,
                    static_cast<int>(number)};
    return &proto_record;
  }
  return nullptr;
}
inline servent *next_service() {
  while (services.next()) {
    char *p = services.tokens[1];
    while (*p && *p != '/')
      ++p;
    if (*p != '/' || !p[1])
      continue;
    *p++ = 0;
    unsigned port;
    if (!decimal(services.tokens[1], 65535, port))
      continue;
    service_record = {
        services.tokens[0], services.tokens + 2,
        static_cast<int>(Endian::to_big_endian(static_cast<uint16_t>(port))),
        p};
    return &service_record;
  }
  return nullptr;
}
} // namespace mmix::netdb
} // namespace LIBC_NAMESPACE_DECL
#endif
