//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_STDLIB_LINUX_MMIX_PROCESS_EXIT_H
#define LLVM_LIBC_SRC_STDLIB_LINUX_MMIX_PROCESS_EXIT_H
#include "hdr/types/pid_t.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
// PLATFORM elects one normal/quick exit owner without runtime locks; recursive
// exit is immediate. Quick exit also suppresses cancellation without the loader.
void begin_process_exit(int status, bool quick = false);

// Private coordinated-fork hooks, not a complete admission certificate. Query
// with application signals blocked, then independently prove an ordinary active
// unwind root and a Running, registered caller under the registry snapshot.
// The process-lifetime/main owner yields a positive TID during normal
// exit_process's TLS/TSS drain with main's phase Cleaning, or during its
// process callbacks with main's phase Finished. Quick exit and thread exit do
// not supply normal-process provenance. An ordinary worker binds its own
// in-progress drain or returned completion to its attributes, never to main.
// If supplied, during_cleanup is true only for a certified ThreadCallbacks
// owner, false for ProcessCallbacks or failure. Snapshot it before clone to
// select the child adopter; a worker adopter cannot infer phase after promotion.
pid_t normal_exit_fork_owner(bool *during_cleanup = nullptr);

// Child-only, after transport publishes the new TID and coordinated registry
// recovery preserves the caller's CLEANUP_CLAIMED state and callback progress.
// The snapshot must have passed the query and ordinary-root checks above.
// Child adoption must retain the matching Cleaning or Finished main phase,
// or establish that phase for the certified DURING/AFTER-worker survivor.
// Does not acquire/release the process claim, restart callbacks, or repair
// roots.
bool rebind_normal_exit_fork_owner(pid_t previous_tid);

// AFTER-only terminal continuation, entered exclusively from the fresh
// nonreturning last-thread root. No TLS/TSS drain is invoked here. The private
// certificate binds owner, attributes and the entire captured root identity;
// fork must additionally prove the chain and locked sole ExitReady census.
[[noreturn]] void exit_after_thread_cleanup();
pid_t last_thread_exit_fork_owner();
bool rebind_last_thread_exit_fork_owner(pid_t previous_tid);
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
