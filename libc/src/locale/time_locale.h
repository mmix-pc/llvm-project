//===-- Borrowed calendar locale selection ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_TIME_LOCALE_H
#define LLVM_LIBC_SRC_LOCALE_TIME_LOCALE_H
#include "hdr/types/locale_t.h"
#include "src/locale/time_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
const TimeLocaleData *current_time_locale();
const TimeLocaleData *time_locale(locale_t locale);
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
