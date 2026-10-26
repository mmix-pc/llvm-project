//===-- Monetary formatting core -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_MONETARY_FORMATTER_H
#define LLVM_LIBC_SRC_MONETARY_FORMATTER_H

#include "src/__support/arg_list.h"
#include "src/__support/error_or.h"
#include "src/locale/monetary_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Null data selects the built-in C conventions. The caller owns the category
// for the duration of the call; formatting never changes thread selection.
ErrorOr<size_t> format_money(char *output, size_t capacity, const char *format,
                             ArgList &args, const MonetaryLocaleData *data);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
