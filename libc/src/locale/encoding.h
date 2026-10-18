//===-- Locale encoding -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_ENCODING_H
#define LLVM_LIBC_SRC_LOCALE_ENCODING_H
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
#ifdef LIBC_FULL_BUILD
bool locale_is_utf8();
#else
// Overlay builds do not own the host's locale objects. Preserve the standalone
// UTF-8 conversion implementation until host locale interoperation is provided.
inline bool locale_is_utf8() { return true; }
#endif
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
