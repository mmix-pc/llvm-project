//===-- MMIX Linux main-thread lifecycle ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_MAIN_THREAD_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_MAIN_THREAD_H

#include "src/__support/macros/config.h"
#include "src/__support/threads/thread_attributes.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

struct MainThreadState {
  ThreadAttributes attributes;
  // Reserve the entire octa: the kernel writes only the native 32-bit word.
  struct alignas(8) ClearTID {
    uint32_t value = UINT32_MAX;
    uint32_t reserved = 0;
  } clear_tid;
};
static_assert(sizeof(MainThreadState::ClearTID) == 8);

// Post-TP only. The caller prepared the identity, mapping and non-owned stacks.
bool activate_main_thread(MainThreadState &state);

// Called by Linux CRT before constructors or any current_thread() consumer.
// Failure leaves state unpublished. Repeated initialization while active is
// harmless, but initialization after cleanup is rejected.
bool initialize_main_thread();

// Drain the upstream callback/TSS manager once, including recursive calls.
// CRT must invoke this at normal termination, not for _Exit or abnormal exit.
void cleanup_main_thread();

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif
