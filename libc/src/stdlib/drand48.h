//===-- Implementation header for drand48 --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_STDLIB_DRAND48_H
#define LLVM_LIBC_SRC_STDLIB_DRAND48_H

#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
double drand48();
} // namespace LIBC_NAMESPACE_DECL
#endif
