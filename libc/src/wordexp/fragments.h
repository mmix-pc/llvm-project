//===-- Owned expansion fragments -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WORDEXP_FRAGMENTS_H
#define LLVM_LIBC_SRC_WORDEXP_FRAGMENTS_H

#include "src/__support/char_vector.h"
#include "src/wordexp/fields.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Own bytes as well as annotations, so nested assignments cannot invalidate
// a previously expanded environment value. Each append is a distinct origin;
// literal runs with identical quoting may share storage. Input must not alias
// this object's storage. A failed append preserves the existing fragments.
class WordFragments {
  ExpansionPart *storage = nullptr;
  size_t count = 0, capacity = 0, last_capacity = 0;

public:
  WordFragments() = default;
  WordFragments(const WordFragments &) = delete;
  WordFragments &operator=(const WordFragments &) = delete;
  ~WordFragments();
  void clear();
  int append(cpp::string_view value, bool split, bool quoted);
  int flatten(CharVector &output) const;
  cpp::span<const ExpansionPart> parts() const { return {storage, count}; }
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
