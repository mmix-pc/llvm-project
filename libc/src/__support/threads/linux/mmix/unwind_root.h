//===-- MMIX Linux unwind roots --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_UNWIND_ROOT_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_UNWIND_ROOT_H

#include "src/__support/macros/config.h"
#include <stdint.h>

#if defined(LIBC_MMIX_SIGNAL_UNWIND) &&                                       \
    (!defined(LIBC_MMIX_SIGNAL_CLEANUP) ||                                    \
     !defined(_LIBUNWIND_USE_METADATA_POLICY) ||                              \
     (!defined(_LIBUNWIND_MMIX_STARTUP_IMAGES) &&                             \
      !defined(_LIBUNWIND_MMIX_CURSOR_LIFETIME)))
#error "Signal unwind requires matched cleanup, metadata and cursor providers"
#endif

struct UnwindMetadataPolicyToken;

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
struct ThreadControl;
enum class UnwindRootKind { Thread, TerminalThreadCleanup, LastThreadProcess };

// Private runtime state, not a public pthread or kernel ABI.
struct UnwindRoot {
  ThreadControl *owner = nullptr;
  uintptr_t ro = 0, procedure = 0, sp = 0;
  uint64_t chain = 0, generation = 0;
  bool active = false;
  UnwindRootKind kind = UnwindRootKind::Thread;

  bool matches(const UnwindRoot &other) const {
    return active && other.active && owner == other.owner && ro == other.ro &&
           procedure == other.procedure && sp == other.sp &&
           chain == other.chain && generation == other.generation &&
           kind == other.kind;
  }
};

using RootBody = void(void *);
void dispatch_c_cleanup(uintptr_t ro, uintptr_t procedure, uintptr_t sp);
void dispatch_async_cleanup(uintptr_t interrupted_ro, uintptr_t interrupted_sp);
bool has_c_cleanup();
bool validate_fork_cleanup(const UnwindRoot &root);
bool restore_fork_unwind(ThreadControl &control);
bool ordinary_fork_context(ThreadControl &control);
// Parent-only event-spawn admission. Does not authorize copied unwind recovery.
bool active_cleanup_spawn_context(ThreadControl &control);
bool last_thread_fork_context(ThreadControl &control);
bool last_worker_fork_context(ThreadControl &control);
bool terminal_thread_fork_context(ThreadControl &control);
bool terminal_worker_fork_context(ThreadControl &control);
[[gnu::visibility("hidden")]] void
run_with_unwind_root(ThreadControl &control, RootBody *body, void *argument);
// Post-stack drain only. Returns to the still-live completion activation,
// withdrawing this root before finish_thread can establish an AFTER root.
[[gnu::visibility("hidden")]] void
run_with_terminal_cleanup_root(ThreadControl &control, RootBody *body,
                               void *argument);
// Fresh nonreturning activation after the thread root has been consumed. This
// does not initialize cancellation windows or resume a thread cleanup drain.
// An ordinary last worker needs its separate completed-drain/election proof.
[[noreturn, gnu::visibility("hidden")]] void
run_with_process_exit_root(ThreadControl &control, RootBody *body, void *argument);

// Shared root bridge. Only the selected forced-exit composition connects public
// pthread_exit here; retained C-only packages keep their direct finish path.
extern "C" [[noreturn, gnu::visibility("hidden")]] void
__llvm_libc_mmix_unwind_to_root(void *result);
[[noreturn, gnu::visibility("hidden")]] void
unwind_after_cleanup_claim(void *result, uint64_t callback_mask);
#ifdef LIBC_MMIX_SIGNAL_UNWIND
// Opt-in direct testcancel admission with all signals blocked. Rejection does
// not consume pending cancellation or change the caller's mask. Success never
// returns. Does not admit a syscall window or an untracked custom restorer.
bool try_signal_cancellation(uint64_t final_mask);
// Trusted private terminal owner only, not public handler cancellation. The
// caller has already claimed cleanup and entered/committed an outermost
// metadata policy token. Token storage must survive ALL discarded activations
// and stacks, through final cursor disposal; known entry bounds are only a
// partial lifetime check. The current segment and EVERY interrupted ancestor
// must have no managed cursor or active query. This is a bounded trusted-caller
// precondition, not the admission policy for a future public interface.
// Requires matched, prebound startup metadata-policy and saved-context hooks.
// All blockable signals must remain blocked throughout stack cleanup, including
// callbacks and destructors. Public admission/mask policy is a separate task.
// Restore final_mask | CANCEL_MASK only before normal TLS/TSS root capture.
[[noreturn, gnu::visibility("hidden")]] void
unwind_signal_after_cleanup_claim(void *result, UnwindMetadataPolicyToken *token,
                                 uint64_t final_mask);
#endif
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
