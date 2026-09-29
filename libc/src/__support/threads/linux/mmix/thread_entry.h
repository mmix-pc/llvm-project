//===-- MMIX Linux native thread boundaries --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_ENTRY_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_ENTRY_H

#include "src/__support/threads/linux/mmix/tls.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

struct ThreadControl;
static_assert(__builtin_offsetof(TLSHeader, thread_state) == 8);

// Only the parent returns here, with a positive TID or raw negative errno.
// The child enters thread_start on its fresh kernel-created register stack.
// All mappings and the TCB control pointer must be prepared before this call.
extern "C" __attribute__((visibility("hidden"))) long
__llvm_libc_mmix_clone_thread(unsigned long flags, uintptr_t stack,
                              uint32_t *parent_tid, uint32_t *clear_tid,
                              uintptr_t thread_pointer);

extern "C" [[noreturn]] __attribute__((visibility("hidden"))) void
__llvm_libc_mmix_thread_start(ThreadControl *control);

// Requires completed cleanup and terminal ownership preparation. This is
// task exit with zero kernel status, not exit_group. The thread's actual return
// value is already in its control record. Live mappings must remain owned.
extern "C" [[noreturn]] __attribute__((visibility("hidden"))) void
__llvm_libc_mmix_thread_exit();

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
