//===-- Owned word expansion source ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/source.h"
#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "hdr/func/malloc.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/alloc-checker.h"
#include "src/string/memory_utils/inline_memcpy.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
struct Decoded {
  size_t count = 0, character_end = 0;
  mbstate state{};
};

ErrorOr<Decoded> decode(cpp::string_view input, RestartableConverter converter,
                        uint32_t *scalars, WordSource::Unit *units) {
  Decoded result;
  size_t at = 0;
  while (at < input.size()) {
    mbstate before = result.state;
    uint32_t scalar;
    auto length =
        converter.decode(&scalar,
                         {reinterpret_cast<const uint8_t *>(input.data() + at),
                          input.size() - at},
                         result.state);
    if (!length)
      return Error(length.error());
    if (*length == size_t(-2)) {
      if (!converter.boundary(result.state))
        return Error(EILSEQ);
      break;
    }
    if (!*length || *length > input.size() - at || !scalar)
      return Error(EILSEQ);
    if (scalars) {
      scalars[result.count] = scalar;
      units[result.count] = {at, at + *length, before, result.state};
    }
    ++result.count;
    at += *length;
    result.character_end = at;
  }
  return result;
}
} // namespace

WordSource::~WordSource() {
  ::free(storage);
  ::free(characters);
  ::free(positions);
}

int WordSource::assign(cpp::string_view input, RestartableConverter converter) {
  // Also reject impossible views before scanning caller storage. Every decoded
  // character consumes at least one byte; this bounds all later allocations.
  if (input.size() > cpp::numeric_limits<size_t>::max() / sizeof(Unit))
    return EOVERFLOW;
  auto counted = decode(input, converter, nullptr, nullptr);
  if (!counted)
    return counted.error();
  WordSource next;
  if (!input.empty()) {
    next.storage = static_cast<char *>(::malloc(input.size() + 1));
    if (!next.storage)
      return ENOMEM;
    inline_memcpy(next.storage, input.data(), input.size());
    next.storage[input.size()] = '\0';
    if (counted->count) {
      AllocChecker ac;
      next.characters = new (ac) uint32_t[counted->count];
      if (!ac)
        return ENOMEM;
      next.positions = new (ac) Unit[counted->count];
      if (!ac)
        return ENOMEM;
    }
    auto filled = decode({next.storage, input.size()}, converter,
                         next.characters, next.positions);
    if (!filled)
      return filled.error();
    next.final_state = filled->state;
  }
  ::free(storage);
  ::free(characters);
  ::free(positions);
  storage = next.storage;
  characters = next.characters;
  positions = next.positions;
  byte_count = input.size();
  count = counted->count;
  character_end = counted->character_end;
  final_state = next.final_state;
  next.storage = nullptr;
  next.characters = nullptr;
  next.positions = nullptr;
  return 0;
}

ErrorOr<cpp::string_view> WordSource::slice(size_t begin, size_t end) const {
  if (begin > end || end > count)
    return Error(EINVAL);
  if (begin == end)
    return cpp::string_view{};
  size_t offset = positions[begin].begin;
  return bytes().substr(offset, positions[end - 1].end - offset);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
