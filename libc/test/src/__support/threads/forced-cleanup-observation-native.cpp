//===-- Native forced cleanup binding regression --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Production libc root/observation and libunwind lifetime helpers. Only the
// kernel, metadata, phase-2 driver and terminal completion are injected. The
// runner replaces the unrelated MMIX signal-transfer SP instruction with a
// trap, never an observation or force_exit body. No target/fork proof is implied.
#include <cassert>
#include <csignal>
#include <cstring>
#include <sys/syscall.h>

// Do not interpose on the host's own exception machinery used for test exit.
#define _Unwind_ForcedUnwind test_forced_unwind
#define _Unwind_GetGR test_get_gr
#define _Unwind_GetCFA test_get_cfa
#define _Unwind_GetRegionStart test_get_region
#include <unwind.h>
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_X86_64_SYSCALL_H
#define __mmix__ 1
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#undef __mmix__
#include "native-unwind-root.cpp"
#undef _Unwind_ForcedUnwind
#undef _Unwind_GetGR
#undef _Unwind_GetCFA
#undef _Unwind_GetRegionStart

namespace M = LIBC_NAMESPACE::mmix;

#if defined(LIBC_MMIX_SHARED_STARTUP) || defined(LIBC_MMIX_SIGNAL_UNWIND) || \
    defined(_LIBUNWIND_MMIX_CURSOR_LIFETIME)
#define TEST_TRACKER 1
// Instrument only the checkpoint service boundary; all provider observation
// and ownership transitions below are the actual production implementation.
#define __libunwind_mmix_cursor_checkpoint provider_checkpoint
#include "mmix/CursorLifetime.cpp"
#undef __libunwind_mmix_cursor_checkpoint
#endif
#ifdef _LIBUNWIND_USE_METADATA_POLICY
#include "UnwindMetadataPolicy.cpp"
#endif

struct _Unwind_Context {
  uintptr_t ro, procedure, sp;
};
static constexpr uint64_t CallbackMask = 0x400;
static uint64_t kernel_chain = 41, signal_mask = CallbackMask;
static bool query_failure, nonempty_base, claimed_entry, async_type;
static unsigned mask_calls, metadata_calls, image_calls, references;
static unsigned base_captures, begins, completions, drives, callbacks;
static M::ForcedCleanupWitness stopped_witness;
static M::ActiveCleanupKind callback_kind = M::ActiveCleanupKind::None;
static bool cleanup_valid = true;
static void *const Result = reinterpret_cast<void *>(0x1238);
struct Completed {};

class InspectionMask {
  uint64_t saved = signal_mask;

public:
  InspectionMask() { signal_mask = UINT64_MAX; }
  ~InspectionMask() { signal_mask = saved; }
};

class ReadOnly {
  unsigned masks = mask_calls, metadata = metadata_calls, images = image_calls;
  unsigned captures = base_captures;
  unsigned char state[sizeof(M::forced)];
#ifdef TEST_TRACKER
  MMIXCursorCheckpoint cursor{};
#endif

public:
  ReadOnly() {
    assert(signal_mask == UINT64_MAX);
    std::memcpy(state, &M::forced, sizeof(state));
#ifdef TEST_TRACKER
    provider_checkpoint(&cursor);
#endif
  }
  ~ReadOnly() {
    assert(masks == mask_calls && metadata == metadata_calls);
    assert(images == image_calls && captures == base_captures);
    assert(!std::memcmp(state, &M::forced, sizeof(state)));
#ifdef TEST_TRACKER
    assert(__libunwind_mmix_cursor_at_checkpoint(&cursor));
#endif
  }
};

static void rejected(const M::ForcedCleanupWitness &saved) {
  ReadOnly readonly;
  M::ForcedCleanupWitness out;
  unsigned char before[sizeof(out)];
  std::memcpy(&out, &saved, sizeof(out));
  std::memcpy(before, &out, sizeof(out));
  assert(!M::snapshot_forced_cleanup(*M::current_control, out));
  assert(!std::memcmp(before, &out, sizeof(out)));
  assert(!M::validate_forced_cleanup(*M::current_control, saved));
}

#ifdef TEST_TRACKER
static void inspect(unsigned expected_phase) {
  InspectionMask mask;
  if (nonempty_base) {
    assert(M::forced.cursor_base.owners);
    rejected(stopped_witness);
    return;
  }
  auto &control = *M::current_control;
  M::ForcedCleanupWitness saved;
  {
    ReadOnly readonly;
    assert(M::snapshot_forced_cleanup(control, saved));
    assert(M::validate_forced_cleanup(control, saved));
    assert(saved.root.matches(control.unwind_root) && saved.result == Result);
    assert(saved.base.thread && !saved.base.owners && !saved.base.query &&
           !saved.base.forced_exception && !saved.base.forced_phase);
    const auto &p = saved.continuation;
    assert(p.phase == expected_phase && p.exception == &M::forced.exception &&
           p.stop == reinterpret_cast<uintptr_t>(&M::stop) &&
           p.argument == &M::forced && !p.base);
    if (expected_phase == MMIXForcedStopped) {
      assert(p.owner && p.cursor && !p.held_token && !p.held_fallback_token);
      stopped_witness = saved;
    } else {
      assert(!p.owner && !p.cursor && !p.token && !p.fallback_token);
      assert(!M::validate_forced_cleanup(control, stopped_witness));
    }
#ifdef _LIBUNWIND_MMIX_STARTUP_IMAGES
    assert((expected_phase == MMIXForcedStopped ? p.token : p.held_token) == 42);
#else
    assert(!p.token && !p.fallback_token && !p.held_token && !p.held_fallback_token);
#endif
    // An observation must not open the existing fork/recovery gates.
    assert(!M::ordinary_fork_context(control));
    assert(!M::restore_fork_unwind(control));
  }

  // Compose actual forced ownership with an injected callback observation.
  // The cleanup observer has its own production-record regression.
  int operation, foreign_operation;
  callback_kind = expected_phase == MMIXForcedStopped
                      ? M::ActiveCleanupKind::CDispatch
                      : M::ActiveCleanupKind::NormalPop;
#ifdef LIBC_MMIX_SHARED_STARTUP
  control.loader_scopes.store(1);
#endif
  assert(!M::capture_active_fork_cleanup(control, nullptr));
  assert(M::capture_active_fork_cleanup(control, &operation));
  assert(M::validate_active_fork_cleanup(control, &operation));
  assert(!M::capture_active_fork_cleanup(control, &foreign_operation));
  assert(!M::validate_active_fork_cleanup(control, &foreign_operation));
  cleanup_valid = false;
  assert(!M::capture_active_fork_cleanup(control, &operation));
  assert(!M::validate_active_fork_cleanup(control, &operation));
  cleanup_valid = true;
#ifdef LIBC_MMIX_SHARED_STARTUP
  control.loader_scopes.store(2);
  assert(!M::capture_active_fork_cleanup(control, &operation));
  assert(!M::validate_active_fork_cleanup(control, &operation));
  control.loader_scopes.store(1);
#endif
  M::end_active_fork_cleanup(&operation);
  assert(!M::validate_active_fork_cleanup(control, &operation));
  callback_kind = M::ActiveCleanupKind::None;
#ifdef LIBC_MMIX_SHARED_STARTUP
  control.loader_scopes.store(0);
#endif
  assert(M::capture_active_fork_cleanup(control, &operation) ==
         (expected_phase == MMIXForcedLanding));
  if (expected_phase == MMIXForcedLanding)
    assert(M::validate_active_fork_cleanup(control, &operation));
  M::end_active_fork_cleanup(&operation);

  // Every saved identity is compared, including unused token slots and the
  // entire pre-forced base. Poisoned borrowed pointers must never be followed.
  for (unsigned field = 0; field != 29; ++field) {
    auto wrong = saved;
    switch (field) {
    case 0: wrong.root.owner = nullptr; break;
    case 1: wrong.root.ro += 8; break;
    case 2: wrong.root.sp += 8; break;
    case 3: wrong.root.procedure += 4; break;
    case 4: ++wrong.root.chain; break;
    case 5: ++wrong.root.generation; break;
    case 6: wrong.root.active = false; break;
    case 7: wrong.root.kind = M::UnwindRootKind::LastThreadProcess; break;
    case 8: wrong.result = nullptr; break;
    case 9: ++wrong.base.thread; break;
    case 10: wrong.base.owners = reinterpret_cast<MMIXCursorLifetime *>(1); break;
    case 11: wrong.base.query = reinterpret_cast<void *>(1); break;
    case 12: wrong.base.forced_exception = reinterpret_cast<void *>(1); break;
    case 13: ++wrong.base.forced_phase; break;
    case 14: ++wrong.continuation.thread; break;
    case 15: wrong.continuation.exception = reinterpret_cast<void *>(1); break;
    case 16: ++wrong.continuation.stop; break;
    case 17: wrong.continuation.argument = reinterpret_cast<void *>(1); break;
    case 18: wrong.continuation.base = reinterpret_cast<MMIXCursorLifetime *>(1); break;
    case 19: wrong.continuation.owner = reinterpret_cast<MMIXCursorLifetime *>(1); break;
    case 20: wrong.continuation.cursor = reinterpret_cast<void *>(1); break;
    case 21: ++wrong.continuation.token; break;
    case 22: ++wrong.continuation.fallback_token; break;
    case 23: ++wrong.continuation.held_token; break;
    case 24: ++wrong.continuation.held_fallback_token; break;
    case 25: ++wrong.continuation.phase; break;
    case 26: wrong.root.generation = 0; break;
    case 27: ++wrong.integer_result; break;
    case 28:
      wrong.result_kind = saved.result_kind == M::ThreadResultKind::Integer
                              ? M::ThreadResultKind::Pointer
                              : M::ThreadResultKind::Integer;
      break;
    }
    ReadOnly readonly;
    assert(!M::validate_forced_cleanup(control, wrong));
  }

  const auto root = control.unwind_root;
  for (unsigned field = 0; field != 9; ++field) {
    switch (field) {
    case 0: control.unwind_root.owner = nullptr; break;
    case 1: control.unwind_root.ro += 8; break;
    case 2: control.unwind_root.sp += 8; break;
    case 3: control.unwind_root.procedure += 4; break;
    case 4: ++control.unwind_root.chain; break;
    case 5: ++control.unwind_root.generation; break;
    case 6: control.unwind_root.active = false; break;
    case 7: control.unwind_root.kind = M::UnwindRootKind::TerminalThreadCleanup; break;
    case 8: control.unwind_root.generation = 0; break;
    }
    rejected(saved);
    control.unwind_root = root;
  }
  auto exception = M::forced.exception;
  for (unsigned field = 0; field != 4; ++field) {
    switch (field) {
    case 0: ++M::forced.exception.exception_class; break;
    case 1: M::forced.exception.exception_cleanup = nullptr; break;
    case 2: M::forced.exception.private_1 = 0; break;
    case 3: M::forced.exception.private_2 = 1; break;
    }
    rejected(saved);
    M::forced.exception = exception;
  }
  auto base = M::forced.cursor_base;
  for (unsigned field = 0; field != 5; ++field) {
    switch (field) {
    case 0: ++M::forced.cursor_base.thread; break;
    case 1: M::forced.cursor_base.owners = reinterpret_cast<MMIXCursorLifetime *>(1); break;
    case 2: M::forced.cursor_base.query = reinterpret_cast<void *>(1); break;
    case 3: M::forced.cursor_base.forced_exception = reinterpret_cast<void *>(1); break;
    case 4: ++M::forced.cursor_base.forced_phase; break;
    }
    rejected(saved);
    M::forced.cursor_base = base;
  }
  ++kernel_chain;
  rejected(saved); // A handler cannot borrow the interrupted ordinary chain.
  --kernel_chain;
  query_failure = true;
  rejected(saved);
  query_failure = false;
  M::current_control = nullptr;
  {
    ReadOnly readonly;
    assert(!M::snapshot_forced_cleanup(control, saved));
    assert(!M::validate_forced_cleanup(control, saved));
  }
  M::current_control = &control;
  M::ThreadControl foreign;
  assert(!M::snapshot_forced_cleanup(foreign, saved));
  assert(!M::validate_forced_cleanup(foreign, saved));

  // Remove the terminal claim for this negative, then rebuild the original
  // pre-claim state. A successful claim disables cancellation and async mode.
  control.cancellation.restore_after_fork();
  rejected(saved);
  control.cancellation.set_enabled(true);
  control.cancellation.set_async(async_type);
  if (claimed_entry)
    assert(control.cancellation.request());
  assert(control.cancellation.claim(claimed_entry));
  assert(!control.cancellation.asynchronous());
  assert(!control.cancellation.enabled());
  M::forced.result = nullptr;
  assert(!M::validate_forced_cleanup(control, saved));
  M::forced.result = Result;

  void *old_query = __libunwind_mmix_cursor_query_enter(&foreign);
  rejected(saved);
  __libunwind_mmix_cursor_query_leave(old_query);
  MMIXCursorLifetime extra{};
  __libunwind_mmix_cursor_begin(&extra, &foreign);
  rejected(saved);
  __libunwind_mmix_cursor_end(&extra);
  const auto old_phase = ::phase();
  ::publish(Changing);
  rejected(saved);
  ::publish(old_phase);
#ifdef _LIBUNWIND_USE_METADATA_POLICY
  UnwindMetadataPolicyToken policy{};
  assert(__unw_metadata_policy_enter(&policy));
  rejected(saved);
  assert(__unw_metadata_policy_commit(&policy));
  rejected(saved);
  assert(__unw_metadata_policy_leave(&policy));
#endif
#ifdef LIBC_MMIX_SIGNAL_UNWIND
  for (unsigned field = 0; field != 10; ++field) {
    switch (field) {
    case 0: M::forced.policy = &M::forced.admission_policy; break;
    case 1: M::forced.admission_policy.owner = 1; break;
    case 2: M::forced.admission_policy.generation = 1; break;
    case 3: M::forced.admission_policy.parent = 1; break;
    case 4: M::forced.admission_policy.previous_mode = UNW_METADATA_PREFLIGHT; break;
    case 5: M::forced.segment.generation = 1; break;
    case 6: M::forced.handoff.phase = M::SignalParentHandoff::Phase::Prepared; break;
    case 7: M::signal_boundary_owner.live = reinterpret_cast<M::SignalBoundary *>(1); break;
    case 8: M::signal_boundary_owner.live_generation = 1; break;
    case 9: M::signal_boundary_owner.handoff_generation = 1; break;
    }
    rejected(saved);
    M::forced.policy = nullptr;
    M::forced.admission_policy = {};
    M::forced.segment = {};
    M::forced.handoff.phase = M::SignalParentHandoff::Phase::Empty;
    M::signal_boundary_owner.live = nullptr;
    M::signal_boundary_owner.live_generation = 0;
    M::signal_boundary_owner.handoff_generation = 0;
  }
  assert(M::forced.empty_cursor.thread == 777); // Not the ordinary base.
#endif
  assert(M::validate_forced_cleanup(control, saved));
}
#endif

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
LIBC_CONSTINIT LIBC_THREAD_LOCAL ThreadControl *current_control;
bool has_c_cleanup() { return false; }
bool snapshot_active_c_cleanup(const UnwindRoot &, ActiveCleanupWitness &out) {
  if (!cleanup_valid || callback_kind == ActiveCleanupKind::None) return false;
  out.kind = callback_kind;
  out.invocation_depth = 1;
  return true;
}
bool validate_active_c_cleanup(const UnwindRoot &,
                               const ActiveCleanupWitness &saved) {
  return cleanup_valid && saved.kind == callback_kind;
}
bool snapshot_pending_cleanup(const UnwindRoot &, PendingCleanupWitness &) {
  return cleanup_valid && callback_kind == ActiveCleanupKind::None;
}
bool validate_pending_cleanup(const UnwindRoot &, const PendingCleanupWitness &) {
  return cleanup_valid && callback_kind == ActiveCleanupKind::None;
}
bool validate_fork_cleanup(const UnwindRoot &) { return true; }
void dispatch_c_cleanup(uintptr_t ro, uintptr_t, uintptr_t) {
  if (ro == current_control->unwind_root.ro)
    return;
  ++callbacks;
  assert(signal_mask == CallbackMask);
#ifdef TEST_TRACKER
  inspect(MMIXForcedStopped);
#else
  InspectionMask mask;
  rejected(stopped_witness);
#endif
}
void begin_thread_cleanup(ThreadControl &control) {
  assert(signal_mask == CallbackMask && forced.active && !claimed_entry);
  ++begins;
  assert(control.cancellation.claim(false));
}
[[noreturn]] void complete_thread_cleanup(ThreadControl &control,
                                        ThreadReturnValue value) {
  ++completions;
  assert(!forced.active && !control.unwind_root.active);
  assert(control.cancellation.terminal() && value.posix_retval == Result);
  assert(signal_mask == CallbackMask);
  throw Completed{};
}
#ifdef LIBC_MMIX_SIGNAL_UNWIND
LIBC_CONSTINIT LIBC_THREAD_LOCAL SignalBoundaryOwner signal_boundary_owner{
    nullptr, 0};
LIBC_CONSTINIT LIBC_THREAD_LOCAL JumpCleanupStack::Node *jump_cleanup_head;
LIBC_CONSTINIT ThreadRegistry thread_registry;
bool ThreadRegistry::try_signal_cleanup_impl(ThreadControl &) {
  __builtin_trap();
}
bool c_cleanup_at_checkpoint(const MmixCleanupLink &) { __builtin_trap(); }
#endif
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

extern "C" long __llvm_libc_mmix_syscall(unsigned long number, long first,
                                         long second, long third, long fourth,
                                         long, long) {
  if (number == __NR_mmix_rstack_query) {
    if (query_failure)
      return -1;
    auto *query = reinterpret_cast<mmix_rstack_query *>(first);
    query->chain_id = kernel_chain;
    query->sigmask = signal_mask;
    return 0;
  }
  assert(number == SYS_rt_sigprocmask && first == SIG_SETMASK && second);
  assert(fourth == sizeof(uint64_t));
  ++mask_calls;
  if (third)
    *reinterpret_cast<uint64_t *>(third) = signal_mask;
  signal_mask = *reinterpret_cast<uint64_t *>(second);
  return 0;
}
extern "C" int unw_getcontext(unw_context_t *) {
  ++metadata_calls;
  return 0;
}
extern "C" int unw_init_local(unw_cursor_t *, unw_context_t *) {
  ++metadata_calls;
  return 0;
}
extern "C" int unw_step(unw_cursor_t *) {
  ++metadata_calls;
  return 1;
}
extern "C" int unw_is_signal_frame(unw_cursor_t *) {
  ++metadata_calls;
  return 0;
}
extern "C" int unw_get_reg(unw_cursor_t *, int reg, unw_word_t *value) {
  ++metadata_calls;
  *value = reg == UNW_MMIX_RO ? 0x1000 : 0x2000;
  return 0;
}
extern "C" int unw_get_proc_info(unw_cursor_t *, unw_proc_info_t *info) {
  ++metadata_calls;
  info->start_ip = reinterpret_cast<uintptr_t>(&M::run_with_unwind_root);
  return 0;
}
extern "C" _Unwind_Word test_get_gr(_Unwind_Context *context, int reg) {
  assert(reg == UNW_MMIX_RO);
  return context->ro;
}
extern "C" _Unwind_Word test_get_cfa(_Unwind_Context *context) {
  return context->sp;
}
extern "C" _Unwind_Ptr test_get_region(_Unwind_Context *context) {
  return context->procedure;
}
#ifdef TEST_TRACKER
extern "C" void __libunwind_mmix_cursor_checkpoint(MMIXCursorCheckpoint *out) {
  if (out == &M::forced.cursor_base) {
    assert(signal_mask == UINT64_MAX && !M::forced.active);
    ++base_captures;
  }
  provider_checkpoint(out);
}
#endif
#ifdef LIBC_MMIX_SIGNAL_UNWIND
extern "C" [[noreturn]] void __llvm_libc_mmix_signal_unwind_landing() {
  __builtin_trap();
}
extern "C" int __libunwind_mmix_init_saved_cursor(unw_cursor_t *,
                                                  unw_context_t *) {
  __builtin_trap();
}
extern "C" _Unwind_Reason_Code __libunwind_mmix_forcedunwind_from_context(
    _Unwind_Exception *, _Unwind_Stop_Fn, void *, unw_context_t *) {
  __builtin_trap();
}
#endif
#ifdef _LIBUNWIND_MMIX_STARTUP_IMAGES
extern "C" bool __libunwind_mmix_acquire_image(uintptr_t, MMIXUnwindImage *view) {
  ++image_calls;
  ++references;
  *view = {};
  view->token = 42;
  return true;
}
extern "C" bool __libunwind_mmix_drop_image(uintptr_t token) {
  ++image_calls;
  assert(token == 42 && references);
  --references;
  return true;
}
extern "C" bool __libunwind_mmix_lookup_image(uintptr_t, uintptr_t,
                                             MMIXUnwindImage *) {
  __builtin_trap();
}
extern "C" bool __libunwind_mmix_retain_image(uintptr_t, MMIXUnwindImage *) {
  __builtin_trap();
}
extern "C" bool __libunwind_mmix_pin_image(uintptr_t) { __builtin_trap(); }
extern "C" bool __libunwind_mmix_find_root_image(uintptr_t, MMIXUnwindImage *) {
  __builtin_trap(); // This fixture does not query mandatory root metadata.
}
extern "C" bool __libunwind_mmix_find_startup_image(uintptr_t, MMIXUnwindImage *) {
  __builtin_trap();
}
#endif

extern "C" _Unwind_Reason_Code test_forced_unwind(
    _Unwind_Exception *exception, _Unwind_Stop_Fn stop, void *argument) {
  ++drives;
  assert(exception == &M::forced.exception && argument == &M::forced);
  assert(stop == M::stop && M::forced.active && signal_mask == CallbackMask);
  assert(M::current_control->cancellation.terminal());
  assert(!M::current_control->cancellation.asynchronous());
  assert(!M::current_control->cancellation.enabled());
  exception->private_1 = reinterpret_cast<uintptr_t>(stop);
  exception->private_2 = reinterpret_cast<uintptr_t>(argument);
  const uintptr_t stop_id = reinterpret_cast<uintptr_t>(stop);
  _Unwind_Context cursor{0x1100, 0x3000, 0x1800};
#ifdef TEST_TRACKER
  assert(base_captures == 1);
  assert(bool(M::forced.cursor_base.owners) == nonempty_base);
  MMIXCursorLifetime lifetime{};
  __libunwind_mmix_cursor_begin(&lifetime, &cursor);
  assert(__libunwind_mmix_forced_enter(&cursor, exception, stop_id, argument));
#ifdef _LIBUNWIND_MMIX_STARTUP_IMAGES
  {
    MMIXCursorQuery query(&cursor);
    MMIXUnwindImage image{};
    bool owned = false;
    assert(mmixFindCursorImage(1000, image, owned) && owned);
  }
#endif
  {
    InspectionMask mask;
    rejected(stopped_witness); // Walking is not a cleanup boundary.
  }
  assert(__libunwind_mmix_forced_stop(&cursor, exception, stop_id, argument, 1));
#else
  assert(!base_captures);
#endif
  constexpr auto actions = _Unwind_Action(_UA_FORCE_UNWIND | _UA_CLEANUP_PHASE);
  assert(stop(1, actions, exception->exception_class, exception, &cursor,
              argument) == _URC_NO_REASON);
#ifdef TEST_TRACKER
  assert(__libunwind_mmix_forced_stop(&cursor, exception, stop_id, argument, 0));
  assert(__libunwind_mmix_forced_landing(&cursor, exception, stop_id, argument));
  {
    InspectionMask mask;
    rejected(stopped_witness); // Landing published, old sidecar not yet retired.
  }
  __libunwind_mmix_cursor_end(&lifetime);
  inspect(MMIXForcedLanding);
  assert(__libunwind_mmix_forced_resume(exception, stop_id, argument));
  __libunwind_mmix_cursor_begin(&lifetime, &cursor);
  assert(__libunwind_mmix_forced_enter(&cursor, exception, stop_id, argument));
  assert(__libunwind_mmix_forced_stop(&cursor, exception, stop_id, argument, 1));
#endif
  // Finish through the actual libc stop/root decision, even for a nonempty
  // initial base. Tests never clear libc forced.active to manufacture progress.
  const auto &root = M::current_control->unwind_root;
  cursor = {root.ro, root.procedure, root.sp};
  stop(1, actions, exception->exception_class, exception, &cursor, argument);
  __builtin_trap();
}

static void exit_body(void *) {
  if (claimed_entry) {
    assert(M::current_control->cancellation.request());
    assert(M::current_control->cancellation.claim(true));
    signal_mask = UINT64_MAX;
    M::unwind_after_cleanup_claim(Result, CallbackMask);
  }
  M::__llvm_libc_mmix_unwind_to_root(Result);
}

static void run_case(bool claimed, bool nonempty, bool asynchronous) {
  claimed_entry = claimed;
  nonempty_base = nonempty;
  async_type = asynchronous;
  base_captures = begins = completions = drives = callbacks = mask_calls = 0;
  signal_mask = CallbackMask;
  M::ThreadControl control;
  control.stack_bottom = 0x1000;
  control.stack_top = 0x3000;
  control.cancellation.set_async(asynchronous);
  M::current_control = &control;
#ifdef LIBC_MMIX_SIGNAL_UNWIND
  M::forced.empty_cursor.thread = 777;
#endif
#ifdef TEST_TRACKER
  int outer_cursor;
  MMIXCursorLifetime outer{};
  if (nonempty)
    __libunwind_mmix_cursor_begin(&outer, &outer_cursor);
#endif
  try {
    M::run_with_unwind_root(control, exit_body, nullptr);
    __builtin_trap();
  } catch (const Completed &) {
    assert(completions == 1 && drives == 1 && callbacks == 1);
    assert(begins == unsigned(!claimed));
    assert(!M::forced.active && !control.unwind_root.active && !references);
    InspectionMask mask;
    rejected(stopped_witness);
  }
#ifdef TEST_TRACKER
  assert(base_captures == 1 && mask_calls == 2);
  // The inherited outer owner is still present and is released only by its
  // own test activation after the real terminal cursor was finished.
  MMIXCursorCheckpoint final{};
  provider_checkpoint(&final);
  assert(final.owners == (nonempty ? &outer : nullptr));
  assert(!final.query && !final.forced_exception && !final.forced_phase);
  if (nonempty)
    __libunwind_mmix_cursor_end(&outer);
#else
  assert(!base_captures && mask_calls == unsigned(claimed));
#endif
}

int main() {
  run_case(false, false, false);
  run_case(true, false, false);
  run_case(false, false, true); // Enabled async type, synchronous explicit exit.
#ifdef TEST_TRACKER
  run_case(false, true, false);
  run_case(true, true, false);
#endif
}
