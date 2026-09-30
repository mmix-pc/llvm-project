//===-- MMIX Linux internal thread creation --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_CREATE_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_CREATE_H

#include "resources.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

struct ThreadCreationResult {
  long error = 0; // Raw negative errno; public pthread mapping is separate.
  ThreadPreparationResult preparation{};
};

// Main must be registered and caller cancellation suppressed through return.
// Failure leaves output unchanged; a failed preclone rollback transfers
// retained descriptors to the caller.
__attribute__((visibility("hidden"))) ThreadCreationResult
create_thread(const ThreadPreparation &request, ThreadAttributes *&output);

// Runtime bootstrap only: bypasses reaper initialization, creates no public
// registry handle or application reservation, and inherits blocked signals.
// The internal runner must never return or invoke application callbacks.
ThreadCreationResult create_thread_helper(const ThreadPreparation &request,
                                         ThreadAttributes *&output);

// Consume the abort owner's creator pin and reclaim only after kernel clear
// and reference drain. Return only after safe reclamation, otherwise terminate.
// The native reclaimer retains failed-release descriptors until fatal handling.
extern "C" __attribute__((visibility("hidden"))) void
__llvm_libc_mmix_reclaim_failed_thread(ThreadControl *control);

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
