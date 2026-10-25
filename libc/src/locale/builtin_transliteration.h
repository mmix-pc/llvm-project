//===-- Embedded neutral transliteration ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_BUILTIN_TRANSLITERATION_H
#define LLVM_LIBC_SRC_LOCALE_BUILTIN_TRANSLITERATION_H

#include "src/locale/transliteration_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Validate once per descriptor acquisition, not once per converted character.
// The returned view borrows process-lifetime read-only bytes; no file or heap
// storage, mutable cache, or global constructor is required.
ErrorOr<TransliterationData> builtin_transliteration();

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_LOCALE_BUILTIN_TRANSLITERATION_H
