//===-- Owned expansion fragments ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/fragments.h"
#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "hdr/func/realloc.h"
#include "src/__support/CPP/limits.h"
#include "src/string/memory_utils/inline_memcpy.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

void WordFragments::clear() {
  for (size_t i = 0; i != count; ++i)
    ::free(const_cast<char *>(storage[i].bytes.data()));
  count = last_capacity = 0;
}

WordFragments::~WordFragments() {
  clear();
  ::free(storage);
}

int WordFragments::append(cpp::string_view value, bool split, bool quoted) {
  constexpr size_t MAX = cpp::numeric_limits<size_t>::max();
  bool join = count && !split && !storage[count - 1].split &&
              storage[count - 1].quoted == quoted;
  size_t previous = join ? storage[count - 1].bytes.size() : 0;
  if (value.size() >= MAX - previous)
    return ENOMEM;
  if (!join && count == capacity) {
    constexpr size_t MAX_PARTS = MAX / sizeof(ExpansionPart);
    if (count == MAX_PARTS)
      return ENOMEM;
    size_t grown =
        capacity ? (capacity > MAX_PARTS / 2 ? MAX_PARTS : capacity * 2) : 8;
    auto *next = static_cast<ExpansionPart *>(
        ::realloc(storage, grown * sizeof(ExpansionPart)));
    if (!next)
      return ENOMEM;
    storage = next;
    capacity = grown;
  }
  char *bytes =
      join ? const_cast<char *>(storage[count - 1].bytes.data()) : nullptr;
  size_t available = join ? last_capacity : 0;
  size_t needed = previous + value.size() + 1;
  if (needed > available) {
    size_t grown = available ? (available > MAX / 2 ? MAX : available * 2) : 32;
    if (grown < needed)
      grown = needed;
    auto *next = static_cast<char *>(::realloc(bytes, grown));
    if (!next)
      return ENOMEM;
    bytes = next;
    available = grown;
  }
  if (!value.empty())
    inline_memcpy(bytes + previous, value.data(), value.size());
  bytes[needed - 1] = '\0';
  storage[join ? count - 1 : count++] = {{bytes, needed - 1}, split, quoted};
  last_capacity = available;
  return 0;
}

int WordFragments::flatten(CharVector &output) const {
  for (const auto &part : parts())
    for (char c : part.bytes)
      if (!output.append(c))
        return ENOMEM;
  return 0;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
