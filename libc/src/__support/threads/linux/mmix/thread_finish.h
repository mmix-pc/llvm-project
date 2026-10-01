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

// Requires CLEANING, completed callbacks and suppressed cancellation. This
// publishes thread completion, not explicit process exit, forced unwinding or
// complete TLS/TSS semantics.
[[noreturn]] __attribute__((visibility("hidden"))) void
finish_thread(ThreadControl &control, ThreadReturnValue result);

// Enter Cleaning under the registry lock, restoring the caller's signal mask
// before any callback or unwinder code. Reentry fails rather than restarting.
void begin_thread_cleanup(ThreadControl &control);
// Called once after normal return or a verified forced-unwind root handoff.
[[noreturn]] void complete_thread_cleanup(ThreadControl &control,
                                        ThreadReturnValue result);

// Normal return and retained C-only explicit exit, without stack unwinding.
[[noreturn]] void exit_thread(ThreadControl &control, ThreadReturnValue result);

// Selected thread cleanup has already completed. Run normal process callbacks
// exactly once on the winning application thread, then exit_group with zero.
[[noreturn]] void terminate_after_thread_cleanup();

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
