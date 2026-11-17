//===-- MMIX Linux legacy host lookup storage --------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC_NETDB_LINUX_MMIX_LEGACY_HOSTS_H
#define LLVM_LIBC_SRC_NETDB_LINUX_MMIX_LEGACY_HOSTS_H
#include "src/__support/net/address.h"
#include "src/netdb/freeaddrinfo.h"
#include "src/netdb/getnameinfo.h"
#include "src/netdb/linux/mmix/resolver.h"
#include "src/stdio/snprintf.h"
namespace LIBC_NAMESPACE_DECL {
namespace mmix::legacy_hosts {
using namespace resolver;
inline thread_local int host_error;
// These legacy interfaces return shared storage, invalidated by the next
// lookup. Callers requiring concurrent results must use getaddrinfo/nameinfo.
inline void *storage;
inline hostent result;
inline hostent *failure(int error) {
  host_error = error == EAI_AGAIN    ? TRY_AGAIN
               : error == EAI_NONAME ? HOST_NOT_FOUND
                                     : NO_RECOVERY;
  if (error == EAI_MEMORY)
    libc_errno = ENOMEM;
  return nullptr;
}
inline hostent *pack(const char *canonical, int family, const Answers &answers,
                     char *const *aliases, size_t alias_count) {
  size_t count = 0, width = family == AF_INET ? 4 : 16;
  for (Address *p = answers.head; p; p = p->next)
    if (p->family == family)
      ++count;
  if (!count) {
    host_error = NO_DATA;
    return nullptr;
  }
  size_t names_size = length(canonical) + 1;
  for (size_t i = 0; i < alias_count; ++i)
    names_size += length(aliases[i]) + 1;
  size_t pointers = count + alias_count + 2;
  // All components originate in bounded resolver/database allocations. Guard
  // the combined size before allocating the single caller-visible record.
  if (count > SIZE_MAX / (sizeof(char *) + width) || pointers < count ||
      pointers > SIZE_MAX / sizeof(char *) ||
      names_size > SIZE_MAX - pointers * sizeof(char *) ||
      count * width > SIZE_MAX - pointers * sizeof(char *) - names_size)
    return failure(EAI_MEMORY);
  void *next = ::malloc(pointers * sizeof(char *) + count * width + names_size);
  if (!next)
    return failure(EAI_MEMORY);
  auto **addresses = static_cast<char **>(next);
  char **alias_list = addresses + count + 1;
  char *bytes = reinterpret_cast<char *>(alias_list + alias_count + 1);
  size_t index = 0;
  for (Address *p = answers.head; p; p = p->next) {
    if (p->family != family)
      continue;
    addresses[index++] = bytes;
    for (size_t i = 0; i < width; ++i)
      bytes[i] = p->bytes[i];
    bytes += width;
  }
  addresses[count] = nullptr;
  char *name = bytes;
  size_t size = length(canonical) + 1;
  copy(bytes, size, canonical);
  bytes += size;
  for (size_t i = 0; i < alias_count; ++i) {
    alias_list[i] = bytes;
    size = length(aliases[i]) + 1;
    copy(bytes, size, aliases[i]);
    bytes += size;
  }
  alias_list[alias_count] = nullptr;
  ::free(storage);
  storage = next;
  result = {name, alias_list, family, static_cast<int>(width), addresses};
  host_error = 0;
  return &result;
}
inline hostent *publish(const char *canonical, int family, Answers &answers,
                        const char *query = nullptr) {
  File file("/etc/hosts");
  while (file.next()) {
    if (!same(file.tokens[1], canonical))
      continue;
    unsigned char bytes[16];
    if (LIBC_NAMESPACE::inet_pton(family, file.tokens[0], bytes) != 1)
      continue;
    return pack(canonical, family, answers, file.tokens + 2, file.count - 2);
  }
  if (file.error)
    return failure(file.error == ENOMEM ? EAI_MEMORY : EAI_SYSTEM);
  char *alias = const_cast<char *>(query);
  return pack(canonical, family, answers, &alias,
              query && !same(query, canonical) ? 1 : 0);
}
inline hostent *forward(const char *name) {
  if (!name || !*name)
    return failure(EAI_NONAME);
  int saved_errno = libc_errno;
  Answers answers;
  auto numeric = net::inet_addr(name);
  if (numeric.has_value()) {
    uint32_t address = *numeric;
    if (!answers.add(AF_INET, &address))
      return failure(EAI_MEMORY);
    hostent *value = pack(name, AF_INET, answers, nullptr, 0);
    if (value)
      libc_errno = saved_errno;
    return value;
  }
  bool matched = false;
  int error = local_addresses(name, answers, matched);
  if (!error && !matched)
    error = dns_lookup(name, true, false, answers);
  if (error) {
    failure(error);
    if (error == EAI_NONAME && answers.no_data)
      host_error = NO_DATA;
    return nullptr;
  }
  hostent *value = publish(answers.canonical[0] ? answers.canonical : name,
                           AF_INET, answers, name);
  if (value)
    libc_errno = saved_errno;
  return value;
}
inline hostent *reverse(const void *address, socklen_t size, int family) {
  if (family != AF_INET && family != AF_INET6) {
    libc_errno = EAFNOSUPPORT;
    return failure(EAI_FAMILY);
  }
  unsigned width = family == AF_INET ? 4 : 16;
  if (!address || size != width) {
    libc_errno = EINVAL;
    return failure(EAI_FAIL);
  }
  int saved_errno = libc_errno;
  char name[256];
  bool matched = false, no_data = false;
  int error = local_reverse(family, address, name, matched);
  if (!error && !matched) {
    const auto *bytes = static_cast<const unsigned char *>(address);
    char query[80];
    if (family == AF_INET) {
      LIBC_NAMESPACE::snprintf(query, sizeof(query), "%u.%u.%u.%u.in-addr.arpa",
                               unsigned(bytes[3]), unsigned(bytes[2]),
                               unsigned(bytes[1]), unsigned(bytes[0]));
    } else {
      const char *hex = "0123456789abcdef";
      size_t at = 0;
      for (int i = 15; i >= 0; --i) {
        query[at++] = hex[bytes[i] & 15];
        query[at++] = '.';
        query[at++] = hex[bytes[i] >> 4];
        query[at++] = '.';
      }
      copy(query + at, sizeof(query) - at, "ip6.arpa");
    }
    error = dns_reverse(query, name, &no_data);
  }
  if (error) {
    failure(error);
    if (error == EAI_NONAME && no_data)
      host_error = NO_DATA;
    return nullptr;
  }
  Answers answers;
  if (!answers.add(family, address))
    return failure(EAI_MEMORY);
  hostent *value = publish(name, family, answers);
  if (value)
    libc_errno = saved_errno;
  return value;
}
} // namespace mmix::legacy_hosts
} // namespace LIBC_NAMESPACE_DECL
#endif
