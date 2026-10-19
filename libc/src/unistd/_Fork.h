//===-- Implementation header for _Fork -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_UNISTD_FORK_MINIMAL_H
#define LLVM_LIBC_SRC_UNISTD_FORK_MINIMAL_H

#include "hdr/types/pid_t.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
pid_t _Fork();
} // namespace LIBC_NAMESPACE_DECL

#endif
