//===-- Implementation header for psiginfo ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SIGNAL_PSIGINFO_H
#define LLVM_LIBC_SRC_SIGNAL_PSIGINFO_H

#include "hdr/types/siginfo_t.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

// Describe a valid siginfo_t, including recognized causes and their applicable
// payload fields. Unknown signal/code combinations do not inspect the payload.
void psiginfo(const siginfo_t *info, const char *message);

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_SIGNAL_PSIGINFO_H
