//===-- Transactional word expansion results --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WORDEXP_RESULTS_H
#define LLVM_LIBC_SRC_WORDEXP_RESULTS_H

#include "hdr/types/wordexp_t.h"
#include "src/__support/CPP/string_view.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Build one batch without changing a previously published APPEND result.
// Strings from that result are borrowed until publication; new strings belong
// to this object. The caller decides whether an error publishes a NOSPACE
// prefix or rolls back. Publication cannot allocate. Public flag validation,
// REUSE disposal and expansion ordering belong to the enclosing entrypoint.
class WordResults {
  char **words = nullptr;
  char **previous_words = nullptr;
  size_t offsets = 0, count = 0, inherited = 0, capacity = 0;
  int reserve(size_t word_count);

public:
  WordResults() = default;
  WordResults(const WordResults &) = delete;
  WordResults &operator=(const WordResults &) = delete;
  ~WordResults();

  // Call once. A nonnull previous is a valid result with the same offsets,
  // and must remain unchanged until publish or destruction.
  int initialize(size_t offsets, const wordexp_t *previous = nullptr);
  int append(cpp::string_view word);
  size_t size() const { return count; }
  void publish(wordexp_t &result);
};

void release_word_results(wordexp_t &result);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
