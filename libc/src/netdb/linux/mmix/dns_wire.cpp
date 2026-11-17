//===-- MMIX Linux DNS wire and configuration -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/netdb/linux/mmix/resolver.h"
namespace LIBC_NAMESPACE_DECL {
namespace mmix::resolver {
static unsigned word(const unsigned char *p) {
  return (unsigned(p[0]) << 8) | p[1];
}
static void put(unsigned char *p, unsigned n) {
  p[0] = static_cast<unsigned char>(n >> 8);
  p[1] = static_cast<unsigned char>(n);
}
size_t make_query(const char *name, unsigned type, uint16_t id,
                  unsigned char packet[512]) {
  for (unsigned i = 0; i < 12; i++)
    packet[i] = 0;
  put(packet, id);
  packet[2] = 1;
  packet[5] = 1;
  size_t at = 12, total = 1;
  const char *p = name;
  if (!*p)
    return 0;
  while (*p) {
    const char *start = p;
    while (*p && *p != '.')
      ++p;
    size_t n = static_cast<size_t>(p - start);
    if (!n || n > 63 || total + n + 1 > 255)
      return 0;
    packet[at++] = static_cast<unsigned char>(n);
    total += n + 1;
    for (size_t i = 0; i < n; i++)
      packet[at++] = static_cast<unsigned char>(start[i]);
    if (*p)
      ++p;
  }
  packet[at++] = 0;
  put(packet + at, type);
  put(packet + at + 2, 1);
  return at + 4;
}
bool decode_name(const unsigned char *packet, size_t size, size_t &offset,
                 char out[256]) {
  size_t at = offset, n = 0, steps = 0, wire = 1;
  bool jumped = false;
  while (at < size && ++steps <= size) {
    unsigned byte = packet[at++];
    if ((byte & 192) == 192) {
      if (at >= size)
        return false;
      size_t target = ((byte & 63) << 8) | packet[at++];
      if (target < 12 || target >= at - 2)
        return false;
      if (!jumped)
        offset = at;
      jumped = true;
      at = target;
      continue;
    }
    if (byte & 192)
      return false;
    if (!byte) {
      if (!jumped)
        offset = at;
      out[n] = 0;
      return true;
    }
    if (at + byte > size || wire + byte + 1 > 255)
      return false;
    wire += byte + 1;
    if (n)
      out[n++] = '.';
    for (unsigned i = 0; i < byte; i++) {
      unsigned char c = packet[at++];
      if (!c || c == '.')
        return false;
      out[n++] = static_cast<char>(c);
    }
  }
  return false;
}
static bool domain(char out[256], const char *s) {
  unsigned char packet[512];
  if (!make_query(s, 1, 0, packet))
    return false;
  size_t n = length(s);
  if (n && s[n - 1] == '.')
    --n;
  if (n >= 256)
    return false;
  for (size_t i = 0; i < n; i++)
    out[i] = static_cast<char>(lower(s[i]));
  out[n] = 0;
  return true;
}
static bool setting(const char *s, const char *key, unsigned low, unsigned high,
                    unsigned &result) {
  while (*key && *key == *s) {
    ++key;
    ++s;
  }
  if (*key || *s++ != ':')
    return false;
  if (!*s)
    return false;
  unsigned value = 0;
  for (; *s; s++) {
    if (*s < '0' || *s > '9')
      return false;
    unsigned d = static_cast<unsigned>(*s - '0');
    if (value <= high)
      value = value * 10 + d;
  }
  result = value < low ? low : value > high ? high : value;
  return true;
}
int read_config(Config &config, const char *path) {
  File file(path);
  while (file.next()) {
    char **t = file.tokens;
    if (netdb::equal(t[0], "nameserver")) {
      Server candidate{};
      auto *v4 = reinterpret_cast<sockaddr_in *>(&candidate.address);
      auto *v6 = reinterpret_cast<sockaddr_in6 *>(&candidate.address);
      if (LIBC_NAMESPACE::inet_pton(AF_INET, t[1], &v4->sin_addr) == 1) {
        v4->sin_family = AF_INET;
        v4->sin_port = Endian::to_big_endian(uint16_t(53));
        candidate.size = sizeof(*v4);
      } else if (LIBC_NAMESPACE::inet_pton(AF_INET6, t[1], &v6->sin6_addr) ==
                 1) {
        v6->sin6_family = AF_INET6;
        v6->sin6_port = Endian::to_big_endian(uint16_t(53));
        candidate.size = sizeof(*v6);
      } else
        continue;
      bool duplicate = false;
      for (unsigned i = 0; i < config.server_count; i++)
        if (candidate.size == config.servers[i].size &&
            bytes_equal(&candidate.address, &config.servers[i].address,
                        candidate.size))
          duplicate = true;
      if (!duplicate && config.server_count < 3)
        config.servers[config.server_count++] = candidate;
    } else if (netdb::equal(t[0], "search") || netdb::equal(t[0], "domain")) {
      char selected[6][256] = {};
      unsigned count = 0;
      size_t end = netdb::equal(t[0], "domain") ? 2 : file.count;
      for (size_t i = 1; i < end && count < 6; i++) {
        char name[256];
        if (!domain(name, t[i]))
          continue;
        bool duplicate = false;
        for (unsigned j = 0; j < count; j++)
          if (same(selected[j], name))
            duplicate = true;
        if (!duplicate)
          copy(selected[count++], 256, name);
      }
      if (count) {
        config.search_count = count;
        for (unsigned i = 0; i < count; i++)
          copy(config.search[i], 256, selected[i]);
      }
    } else if (netdb::equal(t[0], "options")) {
      for (size_t i = 1; i < file.count; i++) {
        setting(t[i], "ndots", 0, 15, config.ndots);
        setting(t[i], "timeout", 1, 30, config.timeout);
        setting(t[i], "attempts", 1, 5, config.attempts);
      }
    }
  }
  if (file.error)
    return file.error == ENOMEM ? EAI_MEMORY : EAI_SYSTEM;
  return config.server_count ? 0 : EAI_FAIL;
}
struct Record {
  size_t offset, data, end;
  unsigned type, klass;
};
static bool record(const unsigned char *p, size_t size, size_t &at, Record &r,
                   char owner[256]) {
  r.offset = at;
  if (!decode_name(p, size, at, owner) || size - at < 10)
    return false;
  r.type = word(p + at);
  r.klass = word(p + at + 2);
  size_t n = word(p + at + 8);
  at += 10;
  if (n > size - at)
    return false;
  r.data = at;
  at += n;
  r.end = at;
  return true;
}
int parse_answer(const unsigned char *p, size_t size, const char *name,
                 unsigned type, uint16_t id, Answers &answer, bool &alias) {
  alias = false;
  answer.no_data = false;
  if (size < 12)
    return EAI_FAIL;
  if (word(p) != id || !(p[2] & 128) || (p[2] & 120) || word(p + 4) != 1)
    return EAI_AGAIN;
  char question[256], normalized[256];
  size_t at = 12;
  if (!decode_name(p, size, at, question) || size - at < 4)
    return EAI_FAIL;
  if (!domain(normalized, name) || !same(question, normalized) ||
      word(p + at) != type || word(p + at + 2) != 1)
    return EAI_AGAIN;
  if (p[2] & 2)
    return EAI_AGAIN;
  at += 4;
  size_t begin = at;
  unsigned count = word(p + 6);
  // Validate every declared record before publishing any answer.
  unsigned all = count + word(p + 8) + word(p + 10);
  for (unsigned i = 0; i < all; i++) {
    Record r;
    char owner[256], target[256];
    if (!record(p, size, at, r, owner))
      return EAI_FAIL;
    if (r.klass == 1 && (r.type == 5 || r.type == 12)) {
      size_t offset = r.data;
      if (!decode_name(p, size, offset, target) || offset != r.end)
        return EAI_FAIL;
    }
    if (r.klass == 1 && ((r.type == 1 && r.end - r.data != 4) ||
                         (r.type == 28 && r.end - r.data != 16)))
      return EAI_FAIL;
  }
  if (at != size)
    return EAI_FAIL;
  unsigned rcode = p[3] & 15;
  if (rcode == 3)
    return EAI_NONAME;
  if (rcode == 2)
    return EAI_AGAIN;
  if (rcode)
    return EAI_FAIL;
  char current[256];
  copy(current, 256, normalized);
  // Resolve CNAMEs independently of record order, bounding cycles by the
  // number of answer records. Additional/authority addresses are not answers.
  for (unsigned hop = 0; hop <= count; hop++) {
    at = begin;
    bool changed = false;
    char next[256] = {};
    for (unsigned i = 0; i < count; i++) {
      Record r;
      char owner[256];
      record(p, size, at, r, owner);
      if (r.klass != 1 || !same(owner, current) || r.type != 5)
        continue;
      size_t offset = r.data;
      char target[256];
      decode_name(p, size, offset, target);
      if (!*target || (changed && !same(target, next)))
        return EAI_FAIL;
      copy(next, 256, target);
      changed = true;
    }
    if (changed) {
      if (hop == count)
        return EAI_FAIL;
      copy(current, 256, next);
      alias = true;
      continue;
    }
    break;
  }
  copy(answer.canonical, 256, current);
  at = begin;
  bool found = false;
  for (unsigned i = 0; i < count; i++) {
    Record r;
    char owner[256];
    record(p, size, at, r, owner);
    if (r.klass != 1 || r.type != type || !same(owner, current))
      continue;
    if (type == 12) {
      size_t offset = r.data;
      char target[256];
      decode_name(p, size, offset, target);
      if (!*target)
        return EAI_FAIL;
      copy(answer.canonical, 256, target);
      found = true;
      break;
    }
    if (type == 1 || type == 28) {
      if (!answer.add(type == 1 ? AF_INET : AF_INET6, p + r.data))
        return EAI_MEMORY;
      found = true;
    }
  }
  if (found) {
    alias = false;
    return 0;
  }
  answer.no_data = !alias;
  return EAI_NONAME;
}
} // namespace mmix::resolver
} // namespace LIBC_NAMESPACE_DECL
