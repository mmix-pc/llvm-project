//===-- Time category construction ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_TIME_CATEGORY_H
#define LLVM_LIBC_SRC_LOCALE_TIME_CATEGORY_H

#include "src/locale/locale_storage.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
ErrorOr<__locale_data *> create_time_category(LocaleDataFile &file);
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_SRC_LOCALE_TIME_CATEGORY_H
