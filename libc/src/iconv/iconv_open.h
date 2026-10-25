//===-- Implementation header for iconv_open ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_ICONV_ICONV_OPEN_H
#define LLVM_LIBC_SRC_ICONV_ICONV_OPEN_H
#include "hdr/types/iconv_t.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
iconv_t iconv_open(const char *to, const char *from);
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_SRC_ICONV_ICONV_OPEN_H
