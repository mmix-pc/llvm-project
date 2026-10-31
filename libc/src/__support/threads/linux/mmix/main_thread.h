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
// Pre-creation fork only, after publishing the child's TID with signals blocked.
bool restore_main_thread_after_fork();
bool adopt_fork_survivor();
// Owner-only phase query. False for workers and for an unfinished TLS/TSS
// drain; this neither proves process-exit ownership nor validates an unwind
// root.
bool main_thread_cleanup_finished();
// Owner-only Cleaning query; false for workers and before/after the drain.
// Does not itself certify normal process exit or an ordinary unwind root.
bool main_thread_cleanup_in_progress();
// Certified normal-exit fork child only: retain Finished and the existing
// attributes rather than restarting main-thread cleanup. Signals remain
// blocked; the caller separately preserves registry progress and rebinds the
// exit owner.
// worker_cleanup is the frozen ordinary-worker AFTER normal-exit certificate:
// after registry promotion, bind its retained attributes and establish Finished.
bool adopt_fork_survivor_after_cleanup(bool worker_cleanup = false);
// Same certified-child requirements, but retain Cleaning and resume the
// existing TLS/TSS drain activation. Never restart or mark the drain finished.
// worker_cleanup requires the DURING certificate: bind the promoted survivor
// as Cleaning; its original cleanup_current_thread worker branch finishes it.
bool adopt_fork_survivor_during_cleanup(bool worker_cleanup = false);
// Sole initial-thread terminal AFTER child. Preserve Finished, closed leases
// and terminal cancellation; only re-register clear-TID and robust state.
bool adopt_fork_survivor_after_thread_exit();
// Elected ordinary last worker AFTER its drain. Preserve worker identity and
// backing; never inspect or rebind the retired initial thread's phase.
bool adopt_fork_survivor_after_worker_exit();
// Sole initial-thread post-stack TLS/TSS continuation, still Cleaning/live one.
bool adopt_fork_survivor_during_thread_exit();
// The transport published the child TID and signals remain blocked. Do not
// acquire locks or repair interrupted subsystems.
bool restore_minimal_fork_child();

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
