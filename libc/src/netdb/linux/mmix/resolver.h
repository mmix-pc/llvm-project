//===-- MMIX Linux reentrant resolver ----------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC_NETDB_LINUX_MMIX_RESOLVER_H
#define LLVM_LIBC_SRC_NETDB_LINUX_MMIX_RESOLVER_H
#include "hdr/func/malloc.h"
#include "hdr/netdb_macros.h"
#include "hdr/types/struct_sockaddr_in.h"
#include "hdr/types/struct_sockaddr_in6.h"
#include "hdr/types/struct_sockaddr_storage.h"
#include "src/netdb/linux/mmix/databases.h"
namespace LIBC_NAMESPACE_DECL {
namespace mmix::resolver {
struct File : netdb::Database {
  explicit File(const char *name) : Database{name} {}
  ~File() { end(); }
  File(const File &) = delete;
  File &operator=(const File &) = delete;
};
inline unsigned char lower(unsigned char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + 32) : c;
}
inline bool same(const char *a, const char *b) {
  while (*a && lower(*a) == lower(*b)) {
    ++a;
    ++b;
  }
  return *a == *b;
}
inline size_t length(const char *s) {
  size_t n = 0;
  while (s[n])
    ++n;
  return n;
}
inline bool copy(char *out, size_t size, const char *s) {
  size_t n = length(s);
  if (n >= size)
    return false;
  for (size_t i = 0; i <= n; i++)
    out[i] = s[i];
  return true;
}
inline bool bytes_equal(const void *a, const void *b, size_t n) {
  auto *x = static_cast<const unsigned char *>(a),
       *y = static_cast<const unsigned char *>(b);
  for (size_t i = 0; i < n; i++)
    if (x[i] != y[i])
      return false;
  return true;
}
struct Address {
  Address *next;
  int family;
  uint32_t scope;
  unsigned char bytes[16];
};
struct Answers {
  Address *head = nullptr, **tail = &head;
  char canonical[256] = {};
  bool no_data = false;
  Answers() = default;
  Answers(const Answers &) = delete;
  Answers &operator=(const Answers &) = delete;
  ~Answers() { clear(); }
  void clear() {
    while (head) {
      Address *p = head;
      head = p->next;
      ::free(p);
    }
    tail = &head;
    canonical[0] = 0;
    no_data = false;
  }
  bool add(int family, const void *data, uint32_t scope = 0) {
    size_t n = family == AF_INET ? 4 : 16;
    for (Address *p = head; p; p = p->next)
      if (p->family == family && p->scope == scope &&
          bytes_equal(p->bytes, data, n))
        return true;
    auto *p = static_cast<Address *>(::malloc(sizeof(Address)));
    if (!p)
      return false;
    *p = {};
    p->family = family;
    p->scope = scope;
    for (size_t i = 0; i < n; i++)
      p->bytes[i] = static_cast<const unsigned char *>(data)[i];
    *tail = p;
    tail = &p->next;
    return true;
  }
};
struct Server {
  sockaddr_storage address{};
  unsigned size = 0;
};
struct Config {
  Server servers[3];
  unsigned server_count = 0;
  char search[6][256] = {};
  unsigned search_count = 0;
  unsigned ndots = 1, timeout = 5, attempts = 2;
};
int read_config(Config &config, const char *path = "/etc/resolv.conf");
// A decoded wire name has at most 253 non-root presentation bytes. Compression
// traversal is bounded by the packet size, including chains containing cycles.
bool decode_name(const unsigned char *packet, size_t size, size_t &offset,
                 char out[256]);
size_t make_query(const char *name, unsigned type, uint16_t id,
                  unsigned char packet[512]);
// Zero is a complete positive, NONAME a definite negative. A CNAME without
// terminal data is returned in canonical for another bounded query.
int parse_answer(const unsigned char *packet, size_t size, const char *name,
                 unsigned type, uint16_t id, Answers &answer, bool &alias);
int dns_lookup(const char *name, bool ipv4, bool ipv6, Answers &answers,
               bool v4_fallback = false);
int dns_reverse(const char *name, char output[256], bool *no_data = nullptr);
int local_addresses(const char *name, Answers &answer, bool &matched);
int local_reverse(int family, const void *address, char output[256],
                  bool &matched);
int configured_families(bool &ipv4, bool &ipv6);
} // namespace mmix::resolver
} // namespace LIBC_NAMESPACE_DECL
#endif
