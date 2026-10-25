//===-- Locale category ownership -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_LOCALE_STORAGE_H
#define LLVM_LIBC_SRC_LOCALE_LOCALE_STORAGE_H

#include "src/locale/locale.h"
#include "src/locale/locale_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Own one reference to a category backed by an immutable file. This factory
// only establishes lifetime; category semantics must be checked before
// selection.
ErrorOr<__locale_data *> create_locale_category(LocaleDataFile &file,
                                                bool utf8);
bool retain_locale_category(__locale_data *data);
void release_locale_category(__locale_data *data);

// The caller must already own or borrow a live object throughout retention.
// Failure rolls back every reference acquired by this call.
bool retain_locale(const __locale_t &locale);
void release_locale(const __locale_t &locale);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_LOCALE_LOCALE_STORAGE_H
