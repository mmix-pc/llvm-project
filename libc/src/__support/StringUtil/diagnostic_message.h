//===-- Locale selection for diagnostic messages ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_STRINGUTIL_DIAGNOSTIC_MESSAGE_H
#define LLVM_LIBC_SRC___SUPPORT_STRINGUTIL_DIAGNOSTIC_MESSAGE_H

#include "hdr/types/locale_t.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
// A null locale selects the caller's locale. Fallback never invalidates the
// original message; successful translations have process lifetime.
const char *get_diagnostic_message(const char *message,
                                   locale_t locale = nullptr);
} // namespace LIBC_NAMESPACE_DECL

#endif
