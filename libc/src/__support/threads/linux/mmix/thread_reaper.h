//===-- MMIX Linux reaper bootstrap -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_REAPER_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_REAPER_H

#include "thread_create.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// Caller holds an application creation reservation, blocks signals and
// suppresses cancellation. Failed attempts finish cleanup before a retry can
// acquire the bootstrap lock; fork preparation may retire an idle helper.
ThreadCreationResult ensure_thread_reaper(const TLSImage &image,
                                         uintptr_t page_size);

// Sole reaper only, with signals/cancellation blocked. One pass handles only
// the initial queue length; delayed clear or references rotate to its tail.
bool reap_detached_pass();
void restore_reaper_after_fork();

// Block signals and cancellation. Call before taking loader/allocator locks.
// A true result proves the sole application caller has no orphan helper state.
bool retire_reaper_for_fork(ThreadControl &caller);
bool reaper_stop_requested();

// Return only after an explicit stop request, outside a reclamation pass.
extern "C" __attribute__((visibility("hidden"))) void
__llvm_libc_mmix_reaper_loop();

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
