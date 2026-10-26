//===-- Owned word expansion source -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WORDEXP_SOURCE_H
#define LLVM_LIBC_SRC_WORDEXP_SOURCE_H

#include "src/__support/CPP/string_view.h"
#include "src/__support/wchar/restartable_converter.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

class WordSource {
public:
  struct Unit {
    size_t begin, end;
    mbstate before, after;
  };

  WordSource() = default;
  WordSource(const WordSource &) = delete;
  WordSource &operator=(const WordSource &) = delete;
  ~WordSource();

  // Snapshot before expansion services may mutate environment/source storage.
  // Failure preserves the old snapshot. Units retain exact bytes and decoder
  // states, including shift prefixes. Complete shift-only suffixes are retained
  // separately; they are not fabricated characters or silently discarded.
  int assign(cpp::string_view input, RestartableConverter converter);
  cpp::string_view bytes() const { return {storage, byte_count}; }
  cpp::span<const uint32_t> scalars() const { return {characters, count}; }
  cpp::span<const Unit> units() const { return {positions, count}; }
  cpp::string_view suffix() const {
    return byte_count ? bytes().substr(character_end) : cpp::string_view{};
  }
  const mbstate &end_state() const { return final_state; }

  // Source token offsets are scalar offsets, not byte offsets. The returned
  // view excludes the final shift-only suffix. Splicing separately quoted or
  // expanded spans still requires the caller's encoding-state composition.
  ErrorOr<cpp::string_view> slice(size_t begin, size_t end) const;

private:
  char *storage = nullptr;
  uint32_t *characters = nullptr;
  Unit *positions = nullptr;
  size_t byte_count = 0, count = 0, character_end = 0;
  mbstate final_state{};
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
