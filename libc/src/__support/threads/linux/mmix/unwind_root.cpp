//===-- MMIX Linux owning unwind activations ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "unwind_root.h"
#include "lifecycle.h"
#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "thread_finish.h"
#include <asm/rstack.h>
#include <asm/unistd.h>
#include <libunwind.h>
#include <unwind.h>
#if defined(LIBC_MMIX_SHARED_STARTUP) || defined(LIBC_MMIX_SIGNAL_UNWIND)
#include "mmix/CursorLifetime.h"
#endif
#if defined(LIBC_MMIX_CANCELLATION_WINDOWS) || defined(LIBC_MMIX_SIGNAL_UNWIND)
#include "cancellation_point.h"
#endif
#ifdef LIBC_MMIX_SIGNAL_UNWIND
#include "UnwindMetadataPolicy.h"
#include "mmix/ForcedContext.h"
#include "src/setjmp/mmix/linux/jump_cleanup.h"
#include "src/signal/linux/mmix/signal_entry.h"

extern "C" [[noreturn, gnu::visibility("hidden")]] void
__llvm_libc_mmix_signal_unwind_landing();
#endif

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {
struct ForcedExit {
  _Unwind_Exception exception{};
  UnwindRoot root{};
  void *result = nullptr;
  bool active = false;
#ifdef LIBC_MMIX_SIGNAL_UNWIND
  UnwindMetadataPolicyToken admission_policy{};
  UnwindMetadataPolicyToken *policy = nullptr;
  uint64_t final_mask = 0;
  SignalBoundaryPoint segment{};
  SignalParentHandoff handoff{};
  unw_context_t parent_context{};
  MMIXCursorCheckpoint empty_cursor{};
#endif
};
LIBC_CONSTINIT LIBC_THREAD_LOCAL ForcedExit forced
#ifdef LIBC_MMIX_SIGNAL_UNWIND
    __attribute__((tls_model("initial-exec")))
#endif
    ;

uint64_t current_chain() {
  mmix_rstack_query query{};
  if (syscall_impl(__NR_mmix_rstack_query, reinterpret_cast<long>(&query)) ||
      !query.chain_id)
    __builtin_trap();
  return query.chain_id;
}

#ifdef LIBC_MMIX_SIGNAL_UNWIND
bool signal_admission(ThreadControl &control, uint64_t chain) {
  const auto &root = control.unwind_root;
  const auto &owner = signal_boundary_owner;
  if (forced.active || forced.policy || forced.admission_policy.generation ||
      forced.handoff.phase != SignalParentHandoff::Phase::Empty ||
      !root.active || root.owner != &control || !root.generation ||
      root.kind != UnwindRootKind::Thread || !root.chain || chain == root.chain ||
      !control.cancellation.ready() || control.cancellation.asynchronous() ||
      control.cancellation_suppression.load() ||
      control.cancellation_transfer.load() ||
#ifdef LIBC_MMIX_SHARED_STARTUP
      control.loader_scopes.load() || control.loader_callbacks.load() ||
#endif
      owner.thread != &control ||
      owner.base_chain != root.chain || owner.handoff_generation || !owner.live ||
      __unw_metadata_policy_current() != UNW_METADATA_ORDINARY)
    return false;
  MMIXCursorCheckpoint checkpoint{};
  __libunwind_mmix_cursor_checkpoint(&checkpoint);
  if (checkpoint.owners || checkpoint.query)
    return false;
  uint64_t generation = owner.live_generation, newer = 0;
  for (auto *base = owner.live; base; base = base->previous) {
    auto &entry = *static_cast<SignalEntryRecord *>(base);
    const auto &point = entry.point;
    if (entry.owner != &owner || point.thread != &control ||
        !point.generation || point.generation != generation ||
        point.generation > owner.generation ||
        (newer && point.generation >= newer) || point.child_chain != chain ||
        !point.parent_chain || point.child_chain == point.parent_chain ||
        !point.wrapper.contains(&entry, sizeof(entry)) ||
        !point.wrapper.disjoint(&forced, sizeof(forced)) ||
        !entry.cleanup_tracked ||
        entry.cancellation_publication != &control.cancellation_point ||
        !__libunwind_mmix_cursor_at_checkpoint(&entry.parent_cursor) ||
        bool(entry.previous) != bool(entry.previous_generation))
      return false;
    chain = point.parent_chain;
    newer = point.generation;
    generation = entry.previous_generation;
  }
  return chain == root.chain && !generation;
}

bool signal_metadata_preflight() {
  // Do not step a suspended ancestor: its pending register-stack words may
  // not yet be in backing memory. The kernel materializes them on the later
  // terminal transfer. Check only its exact-PC FDE here; terminal phase2 owns
  // all subsequent steps and fails closed on missing metadata.
  for (auto *base = signal_boundary_owner.live; base; base = base->previous) {
    unw_cursor_t cursor;
    MMIXCursorLifetime lifetime{};
    __libunwind_mmix_cursor_begin(&lifetime, &cursor);
    unw_proc_info_t info{};
    const bool available =
        !__libunwind_mmix_init_saved_cursor(&cursor, &base->saved_parent) &&
        !unw_get_proc_info(&cursor, &info);
    __libunwind_mmix_cursor_end(&lifetime);
    if (!available)
      return false;
  }
  unw_context_t context;
  unw_cursor_t cursor;
  MMIXCursorLifetime lifetime{};
  __libunwind_mmix_cursor_begin(&lifetime, &cursor);
  bool available = !unw_getcontext(&context) && !unw_init_local(&cursor, &context);
  const auto &wrapper = current_signal_entry()->point.wrapper;
  uintptr_t newer_ro = UINTPTR_MAX;
  while (available) {
    unw_word_t ro, sp;
    unw_proc_info_t info{};
    available = !unw_get_reg(&cursor, UNW_MMIX_RO, &ro) &&
                !unw_get_reg(&cursor, UNW_REG_SP, &sp) &&
                !unw_get_proc_info(&cursor, &info);
    if (!available)
      break;
    if (ro == wrapper.ro) {
      available = sp == wrapper.sp && info.start_ip == wrapper.procedure;
      break;
    }
    if (ro < wrapper.ro || ro >= newer_ro || sp > wrapper.sp) {
      available = false;
      break;
    }
    newer_ro = ro;
    available = unw_step(&cursor) == 1;
  }
  __libunwind_mmix_cursor_end(&lifetime);
  return available;
}

uint64_t signal_chain(const ForcedExit &state) {
  auto *control = current_control;
  auto *token = state.policy;
  mmix_rstack_query query{};
  constexpr uint64_t blockable = UINT64_MAX & ~(uint64_t(1) << (SIGKILL - 1)) &
                                ~(uint64_t(1) << (SIGSTOP - 1));
  if (!control || !state.root.matches(control->unwind_root) ||
      state.root.owner != control || state.root.kind != UnwindRootKind::Thread ||
      !state.root.generation || !state.root.chain ||
      !control->cancellation.terminal() ||
      (!control->terminal_cleanup_pending() &&
       !control->worker_cleanup_pending()) ||
      control->terminal_cleanup_in_progress() ||
      control->worker_cleanup_in_progress() ||
      control->cancellation_suppression.load() ||
      control->cancellation_transfer.load() ||
#ifdef LIBC_MMIX_SHARED_STARTUP
      control->loader_scopes.load() || control->loader_callbacks.load() ||
#endif
      !token || token->parent ||
      token->previous_mode != UNW_METADATA_ORDINARY ||
      __unw_metadata_policy_current() != UNW_METADATA_TERMINAL ||
      !__unw_metadata_policy_commit(token) ||
      syscall_impl(__NR_mmix_rstack_query, reinterpret_cast<long>(&query)) ||
      !query.chain_id || query.sigmask != blockable)
    __builtin_trap();
  // Commit is idempotent: after checking TERMINAL it authenticates the token's
  // thread and current generation, without admitting a preflight caller.
  return query.chain_id;
}

SignalEntryRecord *signal_segment(const ForcedExit &state, uint64_t chain) {
  auto &owner = signal_boundary_owner;
  auto *entry = current_signal_entry();
  if (owner.thread != current_control || owner.base_chain != state.root.chain ||
      owner.handoff_generation)
    __builtin_trap();
  if (chain == state.root.chain) {
    if (entry || owner.live_generation || state.segment.generation)
      __builtin_trap();
    return nullptr;
  }
  if (!entry || entry->owner != &owner || !entry->cleanup_tracked ||
      entry->point.thread != current_control ||
      entry->point.child_chain != chain ||
      !entry->point.generation ||
      entry->point.generation != owner.live_generation ||
      entry->point.generation > owner.generation ||
      !entry->point.matches(state.segment) ||
      !entry->point.wrapper.contains(entry, sizeof(*entry)) ||
      entry->cancellation_publication != &current_control->cancellation_point)
    __builtin_trap();
  return entry;
}

[[noreturn]] void transfer_signal_parent(ForcedExit &state,
                                       SignalEntryRecord &entry,
                                       _Unwind_Context *context) {
  const auto point = entry.point;
  if (state.handoff.phase != SignalParentHandoff::Phase::Empty ||
      !c_cleanup_at_checkpoint(entry.parent_cleanup) ||
      !__libunwind_mmix_cursor_finish_at(context, &entry.parent_cursor) ||
      !__libunwind_mmix_cursor_at_checkpoint(&entry.parent_cursor))
    __builtin_trap();
  // No context access after finish. prepare_transfer verifies the live entry
  // is the top jump node and the cancellation publication is its checkpoint;
  // only then can it certify observer disposal and detach both entry lists.
  SignalBoundaryDisposal disposal{&signal_boundary_owner,
                                  point.generation,
                                  point.child_chain,
                                  entry.cleanup_boundary,
                                  true, true, true};
  if (!entry.prepare_transfer(jump_cleanup_head, point, disposal, state.handoff))
    __builtin_trap();
  // The detached entry belongs to the discarded child. Only TLS survives.
  mmix_rstack_jump jump{
      state.handoff.parent_chain,
      reinterpret_cast<uintptr_t>(__llvm_libc_mmix_signal_unwind_landing),
      reinterpret_cast<uintptr_t>(&state.handoff), 1,
      MMIX_RSTACK_JUMP_SETMASK, UINT64_MAX};
  syscall_impl(__NR_mmix_rstack_jump, reinterpret_cast<long>(&jump));
  __builtin_trap();
}
#endif

// The cursor must step to the caller, not infer its window from a fixed offset.
[[gnu::noinline, clang::disable_tail_calls]] void
capture_root(ThreadControl &control, uintptr_t activation, UnwindRootKind kind) {
  unw_context_t context;
  unw_cursor_t cursor;
#if defined(LIBC_MMIX_SHARED_STARTUP) || defined(LIBC_MMIX_SIGNAL_UNWIND)
  MMIXCursorLifetime lifetime{};
  __libunwind_mmix_cursor_begin(&lifetime, &cursor);
#endif
  unw_proc_info_t procedure;
  unw_word_t ro, sp;
  if (unw_getcontext(&context) || unw_init_local(&cursor, &context) ||
      unw_step(&cursor) != 1 || unw_is_signal_frame(&cursor) != 0 ||
      unw_get_reg(&cursor, UNW_MMIX_RO, &ro) ||
      unw_get_reg(&cursor, UNW_REG_SP, &sp) ||
      unw_get_proc_info(&cursor, &procedure) ||
      procedure.start_ip != activation ||
      !ro || !sp || (ro & 7) || (sp & 7))
    __builtin_trap();
  if (!control.retains_resources_until_process_exit() &&
      (sp < control.stack_bottom || sp >= control.stack_top))
    __builtin_trap();
  auto &root = control.unwind_root;
  if (root.active || root.generation == UINT64_MAX)
    __builtin_trap();
  ++root.generation;
  root.owner = &control;
  root.ro = ro;
  root.sp = sp;
  root.procedure = procedure.start_ip;
  root.chain = current_chain();
  root.kind = kind;
  root.active = true;
#if defined(LIBC_MMIX_SHARED_STARTUP) || defined(LIBC_MMIX_SIGNAL_UNWIND)
  __libunwind_mmix_cursor_end(&lifetime);
#endif
}

_Unwind_Reason_Code stop(int version, _Unwind_Action actions,
                         _Unwind_Exception_Class exception_class,
                         _Unwind_Exception *exception,
                         _Unwind_Context *context, void *argument) {
  auto &state = *static_cast<ForcedExit *>(argument);
  auto *control = current_control;
  const auto &saved = state.root;
  if (version != 1 || !state.active || !control || saved.owner != control ||
      !control->unwind_root.active || control->unwind_root.owner != control ||
      saved.generation != control->unwind_root.generation ||
      saved.ro != control->unwind_root.ro ||
      saved.procedure != control->unwind_root.procedure ||
      saved.sp != control->unwind_root.sp ||
      saved.chain != control->unwind_root.chain ||
      (actions & _UA_END_OF_STACK) ||
      !(actions & _UA_FORCE_UNWIND))
    __builtin_trap();
#ifdef LIBC_MMIX_SIGNAL_UNWIND
  SignalEntryRecord *entry = nullptr;
  if (state.policy) {
    if (&state != &forced || exception != &state.exception ||
        exception_class != state.exception.exception_class ||
        !(actions & _UA_CLEANUP_PHASE))
      __builtin_trap();
    entry = signal_segment(state, signal_chain(state));
  } else
#else
  (void)exception_class;
  (void)exception;
#endif
  if (saved.chain != current_chain())
    __builtin_trap();
  dispatch_c_cleanup(_Unwind_GetGR(context, UNW_MMIX_RO),
                     _Unwind_GetRegionStart(context), _Unwind_GetCFA(context));
#ifdef LIBC_MMIX_SIGNAL_UNWIND
  if (state.policy) {
    if (signal_segment(state, signal_chain(state)) != entry)
      __builtin_trap();
    if (entry) {
      const auto &wrapper = entry->point.wrapper;
      auto ro = _Unwind_GetGR(context, UNW_MMIX_RO);
      auto sp = _Unwind_GetCFA(context);
      if (ro < wrapper.ro || sp > wrapper.sp)
        __builtin_trap();
      if (ro == wrapper.ro) {
        if (_Unwind_GetRegionStart(context) != wrapper.procedure ||
            sp != wrapper.sp)
          __builtin_trap();
        transfer_signal_parent(state, *entry, context);
      }
      // Never compare register-stack positions across signal chains.
      return _URC_NO_REASON;
    }
  }
#endif
  if (_Unwind_GetGR(context, UNW_MMIX_RO) == saved.ro &&
      _Unwind_GetRegionStart(context) == saved.procedure) {
    if (_Unwind_GetCFA(context) != saved.sp || has_c_cleanup())
      __builtin_trap();
#ifdef LIBC_MMIX_SIGNAL_UNWIND
    if (state.policy &&
        (jump_cleanup_head || control->cancellation_point.load() ||
         state.handoff.phase != SignalParentHandoff::Phase::Empty))
      __builtin_trap();
#endif
    control->unwind_root.active = false;
    state.active = false;
#if defined(LIBC_MMIX_SHARED_STARTUP) || defined(LIBC_MMIX_SIGNAL_UNWIND)
    // No more cursor metadata is read after the terminal root decision.
    __libunwind_mmix_cursor_finish(context);
#endif
#ifdef LIBC_MMIX_SIGNAL_UNWIND
    if (state.policy) {
      if (!__libunwind_mmix_cursor_at_checkpoint(&state.empty_cursor) ||
          !__unw_metadata_policy_leave(state.policy) ||
          __unw_metadata_policy_current() != UNW_METADATA_ORDINARY)
        __builtin_trap();
      state.policy = nullptr;
      state.parent_context = {};
      uint64_t mask = state.final_mask | CANCEL_MASK;
      state.final_mask = 0;
      if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                       reinterpret_cast<long>(&mask), 0, sizeof(mask)))
        __builtin_trap();
    }
#endif
    // Never return through consumed frames or release the current backing.
    complete_thread_cleanup(*control, ThreadReturnValue(state.result));
  }
  return _URC_NO_REASON;
}

void disposed(_Unwind_Reason_Code, _Unwind_Exception *) { __builtin_trap(); }
} // namespace

#ifdef LIBC_MMIX_SIGNAL_UNWIND
bool try_signal_cancellation(uint64_t final_mask) {
  auto *control = current_control;
  if (!control || !signal_admission(*control, current_chain()))
    return false;
  auto &policy = forced.admission_policy;
  if (!__unw_metadata_policy_enter(&policy))
    return false;
  bool ready = signal_metadata_preflight();
  MMIXCursorCheckpoint checkpoint{};
  __libunwind_mmix_cursor_checkpoint(&checkpoint);
  ready &= !checkpoint.owners && !checkpoint.query;
  for (auto *base = signal_boundary_owner.live; base; base = base->previous)
    ready &= __libunwind_mmix_cursor_at_checkpoint(
                 &static_cast<SignalEntryRecord *>(base)->parent_cursor) != 0;
  if (!ready || !thread_registry.try_signal_cleanup(*control)) {
    if (!__unw_metadata_policy_leave(&policy))
      __builtin_trap();
    return false;
  }
  if (!__unw_metadata_policy_commit(&policy))
    __builtin_trap();
  unwind_signal_after_cleanup_claim(reinterpret_cast<void *>(-1L), &policy,
                                   final_mask);
}

extern "C" [[noreturn, gnu::visibility("hidden")]] void
__llvm_libc_mmix_signal_unwind_resume(void *environment) {
  auto &state = forced;
  auto &handoff = state.handoff;
  if (!state.active || !state.policy || environment != &handoff)
    __builtin_trap();
  uint64_t chain = signal_chain(state);
  if (handoff.child_chain != state.segment.child_chain ||
      handoff.parent_chain != state.segment.parent_chain ||
      !signal_boundary_owner.consume(handoff, current_control, chain,
                                     state.segment.generation))
    __builtin_trap();
  // Copy the one consumed continuation before resetting the reusable packet.
  // Its old cursor was finished before jump, so neither the old exception nor
  // handoff is referenced by a live child unwind activation.
  state.parent_context = handoff.saved_parent;
  handoff.saved_parent = {};
  handoff.owner = nullptr;
  handoff.generation = handoff.child_chain = handoff.parent_chain = 0;
  handoff.phase = SignalParentHandoff::Phase::Empty;
  auto *entry = current_signal_entry();
  state.segment = entry ? entry->point : SignalBoundaryPoint{};
  signal_segment(state, chain);
  if (!__libunwind_mmix_cursor_at_checkpoint(&state.empty_cursor))
    __builtin_trap();
  state.exception = {};
  state.exception.exception_class = 0x4c4c564d50544852ULL; // LLVMPTHR.
  state.exception.exception_cleanup = disposed;
  __libunwind_mmix_forcedunwind_from_context(
      &state.exception, stop, &state, &state.parent_context);
  __builtin_trap();
}

[[noreturn]] void
unwind_signal_after_cleanup_claim(void *result, UnwindMetadataPolicyToken *token,
                                 uint64_t final_mask) {
  auto *control = current_control;
  auto *entry = current_signal_entry();
  if (!control || forced.active || forced.policy || !entry || !token ||
      reinterpret_cast<uintptr_t>(token) % alignof(UnwindMetadataPolicyToken) ||
      reinterpret_cast<uintptr_t>(token) > UINTPTR_MAX - sizeof(*token) ||
      forced.handoff.phase != SignalParentHandoff::Phase::Empty)
    __builtin_trap();
  // These checks cannot establish persistent lifetime. In particular, a token
  // in an older handler body may lie outside its wrapper's known frame bounds.
  uintptr_t sp;
  asm volatile("OR %0, r254, 0" : "=r"(sp));
  auto extent = entry->point.wrapper;
  if (sp > extent.sp)
    __builtin_trap();
  extent.sp = sp;
  if (!extent.disjoint(token, sizeof(*token)))
    __builtin_trap();
  __libunwind_mmix_cursor_checkpoint(&forced.empty_cursor);
  if (forced.empty_cursor.owners || forced.empty_cursor.query)
    __builtin_trap();
  for (auto *record = signal_boundary_owner.live; record;
       record = record->previous) {
    auto *ancestor = static_cast<SignalEntryRecord *>(record);
    if (!ancestor->cleanup_tracked ||
        !record->point.wrapper.contains(ancestor, sizeof(*ancestor)) ||
        !record->point.wrapper.disjoint(token, sizeof(*token)) ||
        !record->point.wrapper.disjoint(&forced, sizeof(forced)) ||
        !__libunwind_mmix_cursor_at_checkpoint(&ancestor->parent_cursor))
      __builtin_trap();
  }
  forced.root = control->unwind_root;
  forced.policy = token;
  forced.segment = entry->point;
  if (!signal_segment(forced, signal_chain(forced)))
    __builtin_trap();
  forced.result = result;
  forced.final_mask = final_mask;
  forced.exception = {};
  forced.exception.exception_class = 0x4c4c564d50544852ULL; // LLVMPTHR.
  forced.exception.exception_cleanup = disposed;
  forced.active = true;
  __libunwind_mmix_set_managed_stop(reinterpret_cast<uintptr_t>(&stop));
  _Unwind_ForcedUnwind(&forced.exception, stop, &forced);
  __builtin_trap();
}
#endif

bool ordinary_fork_context(ThreadControl &control) {
  const auto &root = control.unwind_root;
  if (forced.active || current_control != &control || !root.active ||
      root.owner != &control || !root.generation || !root.chain)
    return false;
  mmix_rstack_query query{};
  return !syscall_impl(__NR_mmix_rstack_query, reinterpret_cast<long>(&query)) &&
         query.chain_id == root.chain;
}

bool active_cleanup_spawn_context(ThreadControl &control) {
  const auto &root = control.unwind_root;
  if (current_control != &control || !forced.active ||
      !forced.root.matches(root) || root.kind != UnwindRootKind::Thread ||
      root.owner != &control || !root.generation || !root.chain ||
      !control.cancellation.terminal() ||
      forced.exception.exception_class != 0x4c4c564d50544852ULL ||
      forced.exception.exception_cleanup != disposed ||
      forced.exception.private_1 != reinterpret_cast<uintptr_t>(&stop) ||
      forced.exception.private_2 != reinterpret_cast<uintptr_t>(&forced))
    return false;
#ifdef LIBC_MMIX_SIGNAL_UNWIND
  if (forced.policy || forced.handoff.phase != SignalParentHandoff::Phase::Empty)
    return false;
#endif
  // A handler interrupting a cleanup cannot borrow the suspended certificate.
  // The parent retains every exception/cursor/lease unchanged across raw spawn.
  mmix_rstack_query query{};
  return !syscall_impl(__NR_mmix_rstack_query, reinterpret_cast<long>(&query)) &&
         query.chain_id == root.chain;
}

bool restore_fork_unwind(ThreadControl &control) {
  const auto &root = control.unwind_root;
  // Fork copies the register-stack chain and stack addresses. Retain the
  // selected thread/finalization root, never an in-progress forced unwind.
  if (forced.active || current_control != &control || !root.active ||
      root.owner != &control || !root.generation ||
      root.chain != current_chain())
    return false;
  return validate_fork_cleanup(root);
}

bool last_thread_fork_context(ThreadControl &control) {
  return control.terminal_cleanup_finished() &&
         control.unwind_root.kind == UnwindRootKind::LastThreadProcess &&
         ordinary_fork_context(control) && !has_c_cleanup() &&
         validate_fork_cleanup(control.unwind_root);
}

bool last_worker_fork_context(ThreadControl &control) {
  return control.last_worker_cleanup_finished() &&
         control.unwind_root.kind == UnwindRootKind::LastThreadProcess &&
         ordinary_fork_context(control) && !has_c_cleanup() &&
         validate_fork_cleanup(control.unwind_root);
}

bool terminal_thread_fork_context(ThreadControl &control) {
  return control.terminal_cleanup_in_progress() &&
         control.unwind_root.kind == UnwindRootKind::TerminalThreadCleanup &&
         ordinary_fork_context(control) && !has_c_cleanup() &&
         validate_fork_cleanup(control.unwind_root);
}

bool terminal_worker_fork_context(ThreadControl &control) {
  return control.worker_cleanup_in_progress() &&
         control.unwind_root.kind == UnwindRootKind::TerminalThreadCleanup &&
         ordinary_fork_context(control) && !has_c_cleanup() &&
         validate_fork_cleanup(control.unwind_root);
}

[[gnu::noinline, clang::disable_tail_calls]] void
run_with_terminal_cleanup_root(ThreadControl &control, RootBody *body,
                               void *argument) {
  if (current_control != &control || !body || forced.active || has_c_cleanup() ||
      control.unwind_root.active ||
      (!control.terminal_cleanup_pending() && !control.worker_cleanup_pending()) ||
      control.cancellation_point.load() || control.cancellation_transfer.load() ||
      control.cancellation_suppression.load())
    __builtin_trap();
  capture_root(control,
               reinterpret_cast<uintptr_t>(&run_with_terminal_cleanup_root),
               UnwindRootKind::TerminalThreadCleanup);
  // Validate the dispatcher and fresh activation before publishing a drain.
  if (!validate_fork_cleanup(control.unwind_root))
    __builtin_trap();
  const auto root = control.unwind_root;
  body(argument);
  if (!root.matches(control.unwind_root) || forced.active || has_c_cleanup())
    __builtin_trap();
  control.unwind_root.active = false;
}

[[gnu::noinline, clang::disable_tail_calls]] void
run_with_unwind_root(ThreadControl &control, RootBody *body, void *argument) {
  if (current_control != &control || !body)
    __builtin_trap();
  capture_root(control, reinterpret_cast<uintptr_t>(&run_with_unwind_root),
               UnwindRootKind::Thread);
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
  initialize_cancellation_windows(control);
#endif
  body(argument);
  if (has_c_cleanup())
    __builtin_trap();
  control.unwind_root.active = false;
}

[[noreturn, gnu::noinline, clang::disable_tail_calls]] void
run_with_process_exit_root(ThreadControl &control, RootBody *body,
                           void *argument) {
  if (current_control != &control || !body || forced.active || has_c_cleanup() ||
      control.unwind_root.active ||
      (!control.terminal_cleanup_finished() &&
       !control.last_worker_cleanup_finished()) ||
      control.cancellation_point.load() || control.cancellation_transfer.load())
    __builtin_trap();
  capture_root(control, reinterpret_cast<uintptr_t>(&run_with_process_exit_root),
               UnwindRootKind::LastThreadProcess);
  // This also rejects an unfinished C cleanup dispatcher. Never reinitialize
  // cancellation for this new root: terminal delivery remains disabled.
  if (!last_thread_fork_context(control) && !last_worker_fork_context(control))
    __builtin_trap();
  body(argument);
  __builtin_trap();
}

[[noreturn]] static void force_exit(void *result, bool cleanup_claimed,
                                  uint64_t callback_mask = 0) {
  auto *control = current_control;
#ifdef LIBC_MMIX_SHARED_STARTUP
  // Constructor/finalizer exit would abandon a published initializing batch.
  if (control && control->loader_callbacks.load())
    __builtin_trap();
#endif
  if (!control || !control->unwind_root.active || forced.active ||
      control->unwind_root.kind != UnwindRootKind::Thread ||
      control->unwind_root.owner != control ||
      control->unwind_root.chain != current_chain() ||
      (cleanup_claimed && !control->cancellation.terminal()))
    __builtin_trap();
  forced.root = control->unwind_root;
  forced.result = result;
  forced.active = true;
  forced.exception.exception_class = 0x4c4c564d50544852ULL; // LLVMPTHR.
  forced.exception.exception_cleanup = disposed;
  if (!cleanup_claimed)
    begin_thread_cleanup(*control);
  else if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                        reinterpret_cast<long>(&callback_mask), 0,
                        sizeof(callback_mask)))
    __builtin_trap();
#if defined(LIBC_MMIX_SHARED_STARTUP) || defined(LIBC_MMIX_SIGNAL_UNWIND)
  __libunwind_mmix_set_managed_stop(reinterpret_cast<uintptr_t>(&stop));
#endif
  _Unwind_ForcedUnwind(&forced.exception, stop, &forced);
  __builtin_trap();
}

extern "C" [[noreturn]] void __llvm_libc_mmix_unwind_to_root(void *result) {
  force_exit(result, false);
}

[[noreturn]] void unwind_after_cleanup_claim(void *result,
                                           uint64_t callback_mask) {
  // The claim transfers a full mask. Publish forced ownership before restoring
  // callback signals, so no handler observes a partially initialized exit.
  force_exit(result, true, callback_mask);
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
