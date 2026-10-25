//===-- Locale collation entry point helpers --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_COLLATION_H
#define LLVM_LIBC_SRC_LOCALE_COLLATION_H

#include "hdr/types/locale_t.h"
#include "hdr/types/size_t.h"
#include "hdr/types/wchar_t.h"
#include "src/__support/macros/config.h"

struct __locale_data;

namespace LIBC_NAMESPACE_DECL {
namespace internal {
const __locale_data *current_collation();
const __locale_data *explicit_collation(locale_t locale);
int collate(const char *left, const char *right, const __locale_data *category);
int collate(const wchar_t *left, const wchar_t *right,
            const __locale_data *category);
size_t collate_transform(char *dest, const char *source, size_t capacity,
                         const __locale_data *category);
size_t collate_transform(wchar_t *dest, const wchar_t *source, size_t capacity,
                         const __locale_data *category);
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_LOCALE_COLLATION_H
