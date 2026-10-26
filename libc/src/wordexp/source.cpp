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
#include "src/wordexp/syntax.h"

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

int WordSource::command(size_t begin, size_t end, bool backquoted, bool quoted,
                        RestartableConverter converter,
                        CharVector &output) const {
  if (begin > end || end > count || output.length())
    return EINVAL;
  if (begin == end)
    return 0;
  auto append = [&output](cpp::string_view bytes) -> int {
    for (char byte : bytes)
      if (!output.append(byte))
        return ENOMEM;
    return 0;
  };
  uint8_t prefix[MAX_LOCALE_STATE_PREFIX_BYTES];
  auto length = converter.prefix(positions[begin].before, prefix);
  if (!length)
    return length.error();
  if (int error = append({reinterpret_cast<const char *>(prefix), *length}))
    return error;
  auto removed_character = [&](size_t i) -> int {
    // Quote removal removes a character, not preceding designation/shift
    // bytes. This also matters for an escaped newline carrying a designation.
    mbstate state = positions[i].before;
    size_t offset = positions[i].begin, prefix_end = offset;
    for (size_t byte = offset; byte < positions[i].end; ++byte) {
      uint32_t scalar;
      auto read = converter.decode(
          &scalar, {reinterpret_cast<const uint8_t *>(storage + byte), 1},
          state);
      if (!read)
        return read.error();
      if (*read != size_t(-2))
        break;
      if (converter.boundary(state))
        prefix_end = byte + 1;
    }
    return append(bytes().substr(offset, prefix_end - offset));
  };
  for (size_t i = begin; i < end; ++i) {
    bool escape = backquoted && characters[i] == '\\' && i + 1 < end &&
                  backquote_escape(characters[i + 1], quoted);
    size_t offset = positions[i].begin;
    if (escape) {
      if (int error = removed_character(i))
        return error;
      ++i;
      if (characters[i] == '\n') {
        if (int error = removed_character(i))
          return error;
        continue;
      }
      offset = positions[i].begin;
    }
    if (int error = append(bytes().substr(offset, positions[i].end - offset)))
      return error;
  }
  mbstate state = positions[end - 1].after;
  uint8_t reset[MAX_LOCALE_CHARACTER_BYTES];
  auto reset_length = converter.encode(0, reset, state);
  if (!reset_length)
    return reset_length.error();
  return append({reinterpret_cast<const char *>(reset), *reset_length - 1});
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
