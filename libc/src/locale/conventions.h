//===-- Numeric and monetary category construction --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_CONVENTIONS_H
#define LLVM_LIBC_SRC_LOCALE_CONVENTIONS_H

#include "src/locale/locale_storage.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// The result owns a backing-file reference and target grouping/currency
// strings.
ErrorOr<__locale_data *> create_numeric_category(LocaleDataFile &file);
ErrorOr<__locale_data *> create_monetary_category(LocaleDataFile &file);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_SRC_LOCALE_CONVENTIONS_H
