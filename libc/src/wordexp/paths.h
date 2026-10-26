//===-- Word expansion field and pathname composition ------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WORDEXP_PATHS_H
#define LLVM_LIBC_SRC_WORDEXP_PATHS_H

#include "src/__support/wchar/restartable_converter.h"
#include "src/wordexp/fields.h"
#include "src/wordexp/results.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Complete one expanded source word, retaining quote annotations until after
// pathname matching. The converter must match the current locale used by glob;
// the caller synchronizes changes to global locale and owns result publication.
// Errors retain an owned prefix in results for the caller's rollback/partial
// publication policy. Literal and unmatched fields retain their original bytes.
int append_word_paths(cpp::span<const ExpansionPart> parts,
                      cpp::span<const FieldDelimiter> delimiters,
                      RestartableConverter converter, WordResults &results);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
