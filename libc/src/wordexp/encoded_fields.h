//===-- State-dependent word field splitting --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WORDEXP_ENCODED_FIELDS_H
#define LLVM_LIBC_SRC_WORDEXP_ENCODED_FIELDS_H

#include "src/__support/wchar/restartable_converter.h"
#include "src/wordexp/fields.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Delimiters must include their decoded scalars, from WordDelimiters. Retain
// original character bytes and insert only boundary-state transitions after
// removing delimiters. Never match inside a character or across part origins.
// Stateless input retains the byte splitter's arbitrary-byte behavior. An
// invalid stateful byte span can remain opaque when not split; subsequent
// character interpretation must not guess its state.
ErrorOr<size_t>
split_encoded_word_fields(cpp::span<const ExpansionPart> parts,
                          cpp::span<const FieldDelimiter> delimiters,
                          RestartableConverter converter, FieldSink sink);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
