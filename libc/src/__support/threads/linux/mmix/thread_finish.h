//===-- MMIX Linux final user-thread completion ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_FINISH_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_FINISH_H

#include "lifecycle.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// Requires CLEANING, completed callbacks and suppressed cancellation. Later
// explicit exit/forced-unwind paths must finish their own cleanup before this
// boundary. This is not a replacement for unwind or complete TLS/TSS semantics.
[[noreturn]] __attribute__((visibility("hidden"))) void
finish_thread(ThreadControl &control, ThreadReturnValue result);

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
