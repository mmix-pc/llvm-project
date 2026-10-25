//===-- Locale-aware display width -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_WCHAR_DISPLAY_WIDTH_H
#define LLVM_LIBC_SRC_WCHAR_DISPLAY_WIDTH_H

#include "hdr/types/wchar_t.h"
#include "src/locale/character.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
// The selected UTF-8 profiles share the pinned charmap widths. Printability
// remains category-specific; C/POSIX does not acquire Unicode classification.
int display_width(wchar_t value, const __locale_data *category);
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_WCHAR_DISPLAY_WIDTH_H
