//===-- Private locale data storage ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/locale_data.h"
#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "src/__support/alloc-checker.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {

uint32_t read_word(const uint8_t *p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 |
         uint32_t(p[3]);
}

bool range(size_t size, size_t offset, size_t count) {
  return offset <= size && count <= size - offset;
}

bool name_byte(uint8_t ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
         (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.' ||
         ch == '@';
}

} // namespace

ErrorOr<uint32_t> LocaleDataView::Item::word(size_t index) const {
  if (kind != Kind::WORDS || index >= bytes.size() / 4)
    return Error(EINVAL);
  return read_word(bytes.data() + index * 4);
}

ErrorOr<LocaleDataView> LocaleDataView::parse(cpp::span<const uint8_t> bytes) {
  const size_t size = bytes.size();
  if (size < HEADER_SIZE + DIRECTORY_SIZE || size > MAX_SIZE || !bytes.data())
    return Error(EINVAL);
  const uint8_t *p = bytes.data();
  constexpr uint8_t MAGIC[] = {'L', 'L', 'V', 'M', 'L', 'C', 'L', 0};
  for (size_t i = 0; i != sizeof(MAGIC); ++i)
    if (p[i] != MAGIC[i])
      return Error(EINVAL);
  if (read_word(p + 8) != 1 || read_word(p + 12) != 0x01020304 ||
      read_word(p + 16) != size || read_word(p + 20) != 6)
    return Error(EINVAL);

  size_t name_offset = read_word(p + 24), name_size = read_word(p + 28);
  if (name_offset != HEADER_SIZE + DIRECTORY_SIZE || name_size < 2 ||
      name_size > 256 || !range(size, name_offset, name_size) ||
      p[name_offset + name_size - 1] != 0)
    return Error(EINVAL);
  for (size_t i = 0; i + 1 < name_size; ++i)
    if (!name_byte(p[name_offset + i]))
      return Error(EINVAL);
  if ((name_size == 2 && p[name_offset] == '.') ||
      (name_size == 3 && p[name_offset] == '.' && p[name_offset + 1] == '.'))
    return Error(EINVAL);

  size_t previous_end = name_offset + name_size;
  for (unsigned category = 0; category != 6; ++category) {
    const uint8_t *d = p + HEADER_SIZE + category * 16;
    size_t offset = read_word(d + 4), length = read_word(d + 8);
    size_t count = read_word(d + 12);
    if (read_word(d) != category || offset < previous_end ||
        !range(size, offset, length) || count > length / ENTRY_SIZE)
      return Error(EINVAL);
    size_t payload_end = count * ENTRY_SIZE;
    uint32_t previous_key = 0;
    for (size_t i = 0; i != count; ++i) {
      const uint8_t *entry = p + offset + i * ENTRY_SIZE;
      uint32_t key = read_word(entry), kind = read_word(entry + 4);
      size_t start = read_word(entry + 8), payload_size = read_word(entry + 12);
      if (key <= previous_key || start < payload_end ||
          !range(length, start, payload_size))
        return Error(EINVAL);
      switch (kind) {
      case uint32_t(Kind::STRING):
        if (!payload_size || p[offset + start + payload_size - 1] != 0)
          return Error(EINVAL);
        for (size_t j = 0; j + 1 < payload_size; ++j)
          if (p[offset + start + j] == 0)
            return Error(EINVAL);
        break;
      case uint32_t(Kind::WORDS):
        if (payload_size % 4)
          return Error(EINVAL);
        break;
      case uint32_t(Kind::BYTES):
        break;
      case uint32_t(Kind::REFERENCE):
        // References name a category, an item key and its terminal kind.
        if (payload_size != 12)
          return Error(EINVAL);
        break;
      default:
        return Error(EINVAL);
      }
      payload_end = start + payload_size;
      previous_key = key;
    }
    previous_end = offset + length;
  }
  LocaleDataView view(bytes);
  // Only terminal references are admitted. This bounds traversal and rejects
  // cycles without recursive parsing or allocation while reading hostile data.
  for (unsigned category = 0; category != 6; ++category) {
    const uint8_t *d = p + HEADER_SIZE + category * 16;
    const uint8_t *section = p + read_word(d + 4);
    size_t count = read_word(d + 12);
    for (size_t i = 0; i != count; ++i) {
      const uint8_t *entry = section + i * ENTRY_SIZE;
      if (read_word(entry + 4) != uint32_t(Kind::REFERENCE))
        continue;
      const uint8_t *ref = section + read_word(entry + 8);
      uint32_t expected = read_word(ref + 8);
      if (expected < uint32_t(Kind::STRING) || expected > uint32_t(Kind::BYTES))
        return Error(EINVAL);
      auto target = view.find_raw(read_word(ref), read_word(ref + 4));
      if (!target || uint32_t(target->kind) != expected)
        return Error(EINVAL);
    }
  }
  return view;
}

const char *LocaleDataView::name() const {
  return reinterpret_cast<const char *>(bytes.data() +
                                        read_word(bytes.data() + 24));
}

ErrorOr<LocaleDataView::Item> LocaleDataView::find(unsigned category,
                                                   uint32_t key) const {
  auto item = find_raw(category, key);
  if (!item || item->kind != Kind::REFERENCE)
    return item;
  const uint8_t *ref = item->bytes.data();
  return find_raw(read_word(ref), read_word(ref + 4));
}

ErrorOr<LocaleDataView::Item> LocaleDataView::find_raw(unsigned category,
                                                       uint32_t key) const {
  if (category >= 6 || !key)
    return Error(EINVAL);
  const uint8_t *d = bytes.data() + HEADER_SIZE + category * 16;
  const uint8_t *section = bytes.data() + read_word(d + 4);
  size_t low = 0, high = read_word(d + 12);
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    const uint8_t *entry = section + mid * ENTRY_SIZE;
    uint32_t entry_key = read_word(entry);
    if (key < entry_key) {
      high = mid;
    } else if (key > entry_key) {
      low = mid + 1;
    } else {
      return Item{Kind(read_word(entry + 4)),
                  {section + read_word(entry + 8), read_word(entry + 12)}};
    }
  }
  return Error(ENOENT);
}

ErrorOr<LocaleDataFile *> LocaleDataFile::copy(cpp::span<const uint8_t> bytes) {
  if (!bytes.data() ||
      bytes.size() <
          LocaleDataView::HEADER_SIZE + LocaleDataView::DIRECTORY_SIZE ||
      bytes.size() > LocaleDataView::MAX_SIZE)
    return Error(EINVAL);
  AllocChecker ac;
  uint8_t *storage = new (ac) uint8_t[bytes.size()];
  if (!ac)
    return Error(ENOMEM);
  for (size_t i = 0; i != bytes.size(); ++i)
    storage[i] = bytes[i];
  auto view = LocaleDataView::parse({storage, bytes.size()});
  if (!view) {
    ::free(storage);
    return Error(view.error());
  }
  auto *file = new (ac) LocaleDataFile(storage, view.value());
  if (!ac) {
    ::free(storage);
    return Error(ENOMEM);
  }
  return file;
}

bool LocaleDataFile::retain() {
  unsigned old = references.load(cpp::MemoryOrder::RELAXED);
  do {
    if (old == ~0U)
      return false;
  } while (!references.compare_exchange_weak(old, old + 1,
                                             cpp::MemoryOrder::RELAXED));
  return true;
}

LocaleDataFile::~LocaleDataFile() { ::free(storage); }

void LocaleDataFile::release() {
  if (references.fetch_sub(1, cpp::MemoryOrder::ACQ_REL) == 1) {
    this->~LocaleDataFile();
    ::free(this);
  }
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
