//===-- Validated numbered message catalog storage ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/nl_types/catalog_data.h"
#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "src/__support/alloc-checker.h"
#include "src/stdlib/heap_sort.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
uint32_t word(const uint8_t *p, bool big) {
  uint32_t result = 0;
  for (unsigned i = 0; i != 4; ++i)
    result = (result << 8) | p[big ? i : 3 - i];
  return result;
}
} // namespace

ErrorOr<MessageCatalog *> MessageCatalog::copy(cpp::span<const uint8_t> input) {
  if (!input.data() || input.size() < 12 || input.size() > MAX_SIZE)
    return Error(EINVAL);
  AllocChecker ac;
  struct Storage {
    uint8_t *bytes;
    Record *index = nullptr;
    ~Storage() {
      ::free(index);
      ::free(bytes);
    }
  } owned{new (ac) uint8_t[input.size()]};
  if (!ac)
    return Error(ENOMEM);
  for (size_t i = 0; i != input.size(); ++i)
    owned.bytes[i] = input[i];
  const uint8_t *p = owned.bytes;
  constexpr uint32_t MAGIC = 0x960408de;
  bool big = word(p, true) == MAGIC;
  if (!big && word(p, false) != MAGIC)
    return Error(EINVAL);
  size_t width = word(p + 4, big), depth = word(p + 8, big);
  // Each cell has three words in each of two byte orders. Divide before
  // multiplying, including on 32-bit hosts.
  if (!width || !depth || width > (input.size() - 12) / 24 / depth)
    return Error(EINVAL);
  size_t cells = width * depth;
  size_t text_offset = 12 + cells * 24;
  size_t text_size = input.size() - text_offset;
  // A single reverse scan bounds termination for every referenced suffix.
  // Rechecking each suffix independently would be quadratic on hostile input.
  size_t terminated_size = text_size;
  while (terminated_size && p[text_offset + terminated_size - 1] != 0)
    --terminated_size;
  owned.index = new (ac) Record[cells];
  if (!ac)
    return Error(ENOMEM);
  size_t count = 0;
  for (size_t i = 0; i != cells; ++i) {
    const uint8_t *little = p + 12 + i * 12;
    const uint8_t *large = little + cells * 12;
    uint32_t values[3];
    for (unsigned j = 0; j != 3; ++j) {
      values[j] = word(little + 4 * j, false);
      if (values[j] != word(large + 4 * j, true))
        return Error(EINVAL);
    }
    if (!values[0]) {
      if (values[1] || values[2])
        return Error(EINVAL);
      continue;
    }
    if (values[0] > 0x80000000U || values[1] > 0x7fffffffU ||
        values[2] >= terminated_size ||
        (uint64_t(values[0]) * values[1]) % width != i % width)
      return Error(EINVAL);
    owned.index[count++] = {values[0], values[1], values[2]};
  }
  auto less = [](const void *a, const void *b) {
    const auto &left = *static_cast<const Record *>(a);
    const auto &right = *static_cast<const Record *>(b);
    return left.set < right.set ||
           (left.set == right.set && left.message < right.message);
  };
  heap_sort(ArrayFixedSize<sizeof(Record)>(owned.index, count), less);
  for (size_t i = 1; i < count; ++i)
    if (owned.index[i - 1].set == owned.index[i].set &&
        owned.index[i - 1].message == owned.index[i].message)
      return Error(EINVAL);
  auto *catalog =
      new (ac) MessageCatalog(owned.bytes, owned.index, count,
                              reinterpret_cast<const char *>(p + text_offset));
  if (!ac)
    return Error(ENOMEM);
  owned.bytes = nullptr;
  owned.index = nullptr;
  return catalog;
}

const char *MessageCatalog::lookup(int set, int message) const {
  if (set < 0 || message < 0)
    return nullptr;
  const uint32_t key = uint32_t(set) + 1;
  size_t low = 0, high = count;
  while (low != high) {
    size_t mid = low + (high - low) / 2;
    const Record &entry = index[mid];
    if (entry.set < key ||
        (entry.set == key && entry.message < uint32_t(message)))
      low = mid + 1;
    else
      high = mid;
  }
  if (low == count || index[low].set != key ||
      index[low].message != uint32_t(message))
    return nullptr;
  return strings + index[low].offset;
}

MessageCatalog::~MessageCatalog() {
  ::free(index);
  ::free(bytes);
}
void MessageCatalog::destroy() {
  this->~MessageCatalog();
  ::free(this);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
