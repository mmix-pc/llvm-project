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

// Internal only. Main must be registered and caller cancellation suppressed
// through return. Public creation remains unavailable until cleanup/reaping
// and cancellation integration are complete. Failure leaves output unchanged;
// a failed preclone rollback transfers retained descriptors to the caller.
__attribute__((visibility("hidden"))) ThreadCreationResult
create_thread(const ThreadPreparation &request, ThreadAttributes *&output);

// Consume the abort owner's creator pin and reclaim only after kernel clear
// and reference drain. Return only after safe reclamation, otherwise terminate.
// No production fallback is supplied before the reclaimer is implemented.
extern "C" __attribute__((visibility("hidden"))) void
__llvm_libc_mmix_reclaim_failed_thread(ThreadControl *control);

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
