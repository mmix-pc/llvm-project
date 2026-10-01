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

#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
#include "src/__support/threads/linux/mmix/lifecycle.h"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace internal {

#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
using MainThreadState = mmix::ThreadControl;
#else
struct MainThreadState;
#endif

// Post-TP only. The caller prepared the identity, mapping and non-owned stacks.
bool activate_main_thread(MainThreadState &state);

// Called by Linux CRT before constructors or any current_thread() consumer.
// Failure leaves state unpublished. Repeated initialization while active is
// harmless, but initialization after cleanup is rejected.
bool initialize_main_thread();

// Main-only callback/TSS cleanup, including recursive calls. Worker and process
// exit paths use cleanup_current_thread, never another thread's main state.
void cleanup_main_thread();

// Drain only the calling thread's selected callbacks, at most once even when
// a callback requests process exit. Does not unwind stacks or clean up peers.
void cleanup_current_thread();

// Shared explicit/last-thread process finalization, implemented by MMIX exit.
[[noreturn]] void exit_process(int status);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif
