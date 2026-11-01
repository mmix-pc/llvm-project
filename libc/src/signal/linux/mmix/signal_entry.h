//===-- MMIX private application signal entry -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SIGNAL_LINUX_MMIX_SIGNAL_ENTRY_H
#define LLVM_LIBC_SRC_SIGNAL_LINUX_MMIX_SIGNAL_ENTRY_H

#include "src/__support/threads/linux/mmix/signal_boundary.h"
#include "src/__support/macros/attributes.h"

extern "C" __attribute__((visibility("hidden"))) void
__llvm_libc_mmix_application_signal_trampoline();

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
// Matched, opt-in runtime component. A live record alone is not cancellation
// admission: consumers must also verify the current chain, dispose observers
// and cursors, and acquire terminal ownership. Custom restorers publish none.
extern LIBC_CONSTINIT LIBC_THREAD_LOCAL SignalBoundaryOwner signal_boundary_owner
    __attribute__((tls_model("initial-exec"), visibility("hidden")));
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
