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
// acquire the bootstrap lock; successful helper resources persist until exit.
ThreadCreationResult ensure_thread_reaper(const TLSImage &image,
                                         uintptr_t page_size);

// The native queue service owns this endpoint. No idle/success stub is a valid
// provider; a returning service is a fatal loss of detached cleanup.
extern "C" __attribute__((visibility("hidden"))) void
__llvm_libc_mmix_reaper_loop();

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
