// Adapted from NetBSD lib/libc/db/hash/hash_page.c at
// 5b0e0d5997518246c017c6577adcb59be12d0a79 (putpair and __delpair).
/*-
 * Copyright (c) 1990, 1993, 1994
 * The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Margo Seltzer.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "hash_page.h"
#include "src/string/memory_utils/inline_memcpy.h"
#include "src/string/memory_utils/inline_memmove.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {
namespace {
// Byte accesses keep both foreign byte order and unaligned buffers safe.
class Page {
  unsigned char *bytes;
  bool be;

public:
  Page(unsigned char *bytes, bool be) : bytes(bytes), be(be) {}
  uint16_t get(size_t index) const {
    const unsigned char *p = bytes + 2 * index;
    return be ? uint16_t(uint16_t(p[0]) << 8 | p[1])
              : uint16_t(uint16_t(p[1]) << 8 | p[0]);
  }
  void set(size_t index, size_t value) {
    bytes[2 * index] = static_cast<unsigned char>(be ? value >> 8 : value);
    bytes[2 * index + 1] = static_cast<unsigned char>(be ? value : value >> 8);
  }
  bool has_overflow() const { return get(0) && get(get(0)) == 0; }
  size_t records() const { return get(0) / 2 - (has_overflow() ? 1 : 0); }
  void finish(size_t count, size_t offset) {
    set(0, count);
    set(count + 1, offset - (count + 3) * 2);
    set(count + 2, offset);
  }
};
bool valid(const unsigned char *bytes, size_t size, bool be) {
  return validate_hash_page(bytes, size, be) == HashPageStatus::Valid;
}
} // namespace

bool initialize_hash_page(unsigned char *bytes, size_t size, bool big_endian) {
  if (!bytes || size != HASH_PAGE_SIZE)
    return false;
  for (size_t i = 0; i < size; ++i)
    bytes[i] = 0;
  Page(bytes, big_endian).finish(0, size);
  return true;
}

bool read_hash_page_info(const unsigned char *bytes, size_t size,
                         bool big_endian, HashPageInfo &out) {
  if (!valid(bytes, size, big_endian))
    return false;
  const Page page(const_cast<unsigned char *>(bytes), big_endian);
  out = {page.records(),
         page.has_overflow() ? page.get(page.get(0) - 1) : uint16_t{0}};
  return true;
}

HashPageResult read_hash_record(const unsigned char *bytes, size_t size,
                                bool big_endian, size_t record,
                                HashRecord &out) {
  if (!valid(bytes, size, big_endian))
    return HashPageResult::Malformed;
  // Only const accessors are used on this view.
  const Page page(const_cast<unsigned char *>(bytes), big_endian);
  if (record >= page.records())
    return HashPageResult::NotFound;
  size_t index = record * 2 + 1;
  size_t previous = index == 1 ? size : page.get(index - 1);
  size_t key = page.get(index), content = page.get(index + 1);
  out = {bytes + key, previous - key, bytes + content, key - content};
  return HashPageResult::Success;
}

HashPageResult append_hash_record(unsigned char *bytes, size_t size,
                                  bool big_endian, const HashRecord &record) {
  if (!hash_record_fits(record.key_size, record.content_size))
    return HashPageResult::RecordTooLarge;
  if ((!record.key && record.key_size) ||
      (!record.content && record.content_size) ||
      !valid(bytes, size, big_endian))
    return HashPageResult::Malformed;
  Page page(bytes, big_endian);
  size_t count = page.get(0);
  size_t free = page.get(count + 1), offset = page.get(count + 2);
  bool overflow = page.has_overflow();
  // Reserve the terminal overflow pair even before a bucket grows a chain.
  size_t required =
      4 + record.key_size + record.content_size + (overflow ? 0 : 4);
  if (required > free)
    return HashPageResult::Full;
  size_t index = overflow ? count - 1 : count + 1;
  uint16_t next = overflow ? page.get(count - 1) : 0;
  offset -= record.key_size;
  if (record.key_size)
    inline_memcpy(bytes + offset, record.key, record.key_size);
  page.set(index, offset);
  offset -= record.content_size;
  if (record.content_size)
    inline_memcpy(bytes + offset, record.content, record.content_size);
  page.set(index + 1, offset);
  if (overflow) {
    page.set(index + 2, next);
    page.set(index + 3, 0);
  }
  page.finish(count + 2, offset);
  return HashPageResult::Success;
}

HashPageResult remove_hash_record(unsigned char *bytes, size_t size,
                                  bool big_endian, size_t record) {
  if (!valid(bytes, size, big_endian))
    return HashPageResult::Malformed;
  Page page(bytes, big_endian);
  if (record >= page.records())
    return HashPageResult::NotFound;
  size_t count = page.get(0), index = record * 2 + 1;
  size_t previous = index == 1 ? size : page.get(index - 1);
  size_t content = page.get(index + 1), offset = page.get(count + 2);
  size_t length = previous - content;
  inline_memmove(bytes + offset + length, bytes + offset, content - offset);
  for (size_t i = index + 2; i <= count; i += 2) {
    if (page.get(i + 1) == 0) {
      page.set(i - 2, page.get(i));
      page.set(i - 1, 0);
    } else {
      page.set(i - 2, page.get(i) + length);
      page.set(i - 1, page.get(i + 1) + length);
    }
  }
  page.finish(count - 2, offset + length);
  return HashPageResult::Success;
}

HashPageResult set_hash_overflow(unsigned char *bytes, size_t size,
                                 bool big_endian, uint16_t address) {
  if (!valid(bytes, size, big_endian) || (address >> 11) >= 31 ||
      (address & 0x7ff) == 0)
    return HashPageResult::Malformed;
  Page page(bytes, big_endian);
  size_t count = page.get(0), offset = page.get(count + 2);
  if (page.has_overflow()) {
    page.set(count - 1, address);
  } else {
    if (page.get(count + 1) < 4)
      return HashPageResult::Full;
    page.set(count + 1, address);
    page.set(count + 2, 0);
    page.finish(count + 2, offset);
  }
  return HashPageResult::Success;
}

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
