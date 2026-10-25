//===-- Locale character entry point helpers --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_CHARACTER_H
#define LLVM_LIBC_SRC_LOCALE_CHARACTER_H

#include "hdr/types/wint_t.h"
#include "src/__support/wctype_impl.h"
#ifdef LIBC_FULL_BUILD
#include "hdr/types/locale_t.h"
#endif

struct __locale_data;

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Borrow the selected immutable category for one call. Overlay builds never
// inspect host locale objects and retain their configured built-in behavior.
const __locale_data *current_ctype();
#ifdef LIBC_FULL_BUILD
const __locale_data *explicit_ctype(locale_t locale);
#endif
int classify_byte(int c, wctype_t desc, const __locale_data *category);
int classify_wide(wint_t c, wctype_t desc, const __locale_data *category);
int map_byte(int c, bool upper, const __locale_data *category);
wint_t map_wide(wint_t c, bool upper, const __locale_data *category);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_LOCALE_CHARACTER_H
