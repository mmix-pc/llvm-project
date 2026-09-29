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

// Internal explicit exit, including main while peers survive. Caller supplies
// its own control and suppresses cancellation. Automatic-object unwinding and
// complete public TLS/TSS destruction require the later public exit path.
[[noreturn]] void exit_thread(ThreadControl &control, ThreadReturnValue result);

// Selected thread cleanup has already completed. Run normal process callbacks
// exactly once on the winning application thread, then exit_group with zero.
[[noreturn]] void terminate_after_thread_cleanup();

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
