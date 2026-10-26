//===-- Transactional word expansion results ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/results.h"
#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "hdr/func/malloc.h"
#include "hdr/func/realloc.h"
#include "hdr/wordexp_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/string/memory_utils/inline_memcpy.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

int WordResults::reserve(size_t word_count) {
  constexpr size_t MAX_SLOTS =
      cpp::numeric_limits<size_t>::max() / sizeof(char *);
  if (offsets >= MAX_SLOTS || word_count >= MAX_SLOTS - offsets)
    return ENOMEM;
  size_t slots = offsets + word_count + 1;
  if (slots <= capacity)
    return 0;
  size_t grown = capacity <= MAX_SLOTS / 2 ? capacity * 2 : MAX_SLOTS;
  if (slots < grown)
    slots = grown;
  auto **next = static_cast<char **>(::realloc(words, slots * sizeof(char *)));
  if (!next)
    return ENOMEM;
  words = next;
  capacity = slots;
  return 0;
}

int WordResults::initialize(size_t skipped, const wordexp_t *previous) {
  offsets = skipped;
  if (previous && previous->we_offs != offsets)
    return EINVAL;
  size_t old_count = previous ? previous->we_wordc : 0;
  int error = reserve(old_count);
  if (error)
    return error;
  for (size_t i = 0; i < offsets; ++i)
    words[i] = nullptr;
  for (size_t i = 0; i < old_count; ++i)
    words[offsets + i] = previous->we_wordv[offsets + i];
  count = inherited = old_count;
  previous_words = previous ? previous->we_wordv : nullptr;
  words[offsets + count] = nullptr;
  return 0;
}

int WordResults::append(cpp::string_view word) {
  if (word.size() == cpp::numeric_limits<size_t>::max() ||
      count == cpp::numeric_limits<size_t>::max())
    return ENOMEM;
  // Reserve first: even failure to copy a word leaves a publishable prefix.
  int error = reserve(count + 1);
  if (error)
    return error;
  char *copy = static_cast<char *>(::malloc(word.size() + 1));
  if (!copy)
    return ENOMEM;
  if (!word.empty())
    inline_memcpy(copy, word.data(), word.size());
  copy[word.size()] = '\0';
  words[offsets + count++] = copy;
  words[offsets + count] = nullptr;
  return 0;
}

void WordResults::publish(wordexp_t &result) {
  ::free(previous_words);
  result.we_wordc = count;
  result.we_wordv = words;
  result.we_offs = offsets;
  words = previous_words = nullptr;
  count = inherited = capacity = 0;
}

WordResults::~WordResults() {
  if (!words)
    return;
  for (size_t i = inherited; i < count; ++i)
    ::free(words[offsets + i]);
  ::free(words);
}

void release_word_results(wordexp_t &result) {
  if (result.we_wordv) {
    for (size_t i = 0; i < result.we_wordc; ++i)
      ::free(result.we_wordv[result.we_offs + i]);
    ::free(result.we_wordv);
  }
  result.we_wordv = nullptr;
  result.we_wordc = 0;
}

int WordResultTransaction::begin(int flags) {
  constexpr int KNOWN = WRDE_DOOFFS | WRDE_APPEND | WRDE_NOCMD | WRDE_REUSE |
                        WRDE_SHOWERR | WRDE_UNDEF;
  if ((flags & ~KNOWN) ||
      (flags & (WRDE_APPEND | WRDE_REUSE)) == (WRDE_APPEND | WRDE_REUSE))
    return WRDE_SYNTAX;
  size_t offsets = flags & WRDE_DOOFFS ? target.we_offs : 0;
  if (flags & WRDE_REUSE)
    release_word_results(target);
  bool append = flags & WRDE_APPEND;
  if (!append) {
    target.we_wordc = 0;
    target.we_wordv = nullptr;
    target.we_offs = offsets;
  }
  int error = pending.initialize(offsets, append ? &target : nullptr);
  ready = error == 0;
  return error == ENOMEM ? WRDE_NOSPACE : error ? WRDE_SYNTAX : 0;
}

int WordResultTransaction::finish(int status) {
  if (ready && (!status || status == WRDE_NOSPACE)) {
    pending.publish(target);
    ready = false;
  }
  return status;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
