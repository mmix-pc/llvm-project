//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_UTMPX_GETUTXLINE_H
#define LLVM_LIBC_SRC_UTMPX_GETUTXLINE_H

#include "hdr/types/struct_utmpx.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

struct utmpx *getutxline(const struct utmpx *key);

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_UTMPX_GETUTXLINE_H
