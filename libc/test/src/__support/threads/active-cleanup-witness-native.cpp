//===-- Native active registered cleanup witness regression ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Production cleanup.cpp and CleanupOwners, injected kernel/cursor/services.
// Frame capture is synthetic. No target exception, registry, loader retirement
// or child admission proof is supplied by this fixture.
#include <cassert>
#include <csignal>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <type_traits>
#include <unistd.h>

#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_X86_64_SYSCALL_H
#define __mmix__ 1
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#undef __mmix__
#include "src/__support/threads/linux/mmix/cleanup.cpp"

namespace M = LIBC_NAMESPACE::mmix;
namespace L = LIBC_NAMESPACE::loader;

static uint64_t chain = 41, signal_mask = 0x400;
static bool query_failure, expect_witness, copy_process, copied_child;
static M::ActiveCleanupKind expected_kind = M::ActiveCleanupKind::CDispatch;
static unsigned calls, acquired, released, scopes, mask_changes, cursor_captures;
static pid_t child_pid;
static MmixCleanupFrame capture_frame;
static M::ActiveCleanupWitness previous;
struct Slot { bool live; };
static Slot slots[64];

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
LIBC_CONSTINIT LIBC_THREAD_LOCAL ThreadControl *current_control;
#ifdef LIBC_MMIX_SIGNAL_CLEANUP
LIBC_CONSTINIT LIBC_THREAD_LOCAL SignalBoundaryOwner signal_boundary_owner{
    nullptr, 0};
#ifdef LIBC_MMIX_SHARED_STARTUP
int set_cancel_state(int state, int *) {
  current_control->cancellation.set_enabled(state == PTHREAD_CANCEL_ENABLE);
  return 0;
}
#endif
#endif

long retain_shared_signal(uintptr_t callback, uintptr_t restorer,
                          loader::LoaderSignalLease *lease) {
  assert(signal_mask == UINT64_MAX && callback && !restorer);
  assert(!lease->snapshot && !lease->images[0] && !lease->images[1]);
  assert(acquired < 64);
  auto *slot = &slots[acquired++];
  assert(!slot->live);
  slot->live = true;
  *lease = {slot, {slot, nullptr}};
  return 0;
}
void finish_shared_signal(loader::LoaderSignalLease *lease, bool installed) {
  assert(signal_mask == UINT64_MAX && !installed);
  assert(lease->snapshot && lease->images[0] == lease->snapshot);
  if (active_dispatch)
    assert(active_dispatch->entry.ownership.lease.snapshot != lease->snapshot);
  auto *slot = static_cast<Slot *>(lease->snapshot);
  assert(slot->live);
  slot->live = false;
  ++released;
  *lease = {};
}
#ifdef LIBC_MMIX_SHARED_STARTUP
static long enter(uint64_t *saved) {
  assert(signal_mask == UINT64_MAX);
  *saved = current_control->cancellation.set_enabled(false);
  ++scopes;
  return 0;
}
static long leave(uint64_t saved) {
  assert(scopes);
  --scopes;
  current_control->cancellation.set_enabled(saved);
  return 0;
}
loader::LoaderCancellation loader_cancellation_hooks() { return {enter, leave}; }
#endif
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

extern "C" long __llvm_libc_mmix_syscall(unsigned long number, long first,
                                         long second, long third, long,
                                         long, long) {
  if (number == __NR_mmix_rstack_query) {
    if (query_failure)
      return -1;
    reinterpret_cast<mmix_rstack_query *>(first)->chain_id = chain;
    return 0;
  }
  assert(number == SYS_rt_sigprocmask && first == SIG_SETMASK && second);
  ++mask_changes;
  if (third)
    *reinterpret_cast<uint64_t *>(third) = signal_mask;
  signal_mask = *reinterpret_cast<uint64_t *>(second);
  return 0;
}
extern "C" int unw_getcontext(unw_context_t *) {
  ++cursor_captures;
  return 0;
}
extern "C" int unw_init_local(unw_cursor_t *cursor, unw_context_t *) {
  cursor->step = 0;
  return 0;
}
extern "C" int unw_step(unw_cursor_t *cursor) { ++cursor->step; return 1; }
extern "C" int unw_is_signal_frame(unw_cursor_t *) { return 0; }
extern "C" int unw_get_reg(unw_cursor_t *cursor, int reg, unw_word_t *value) {
  *value = reg == UNW_MMIX_RO ? capture_frame.__ro
           : cursor->step >= 3 ? capture_frame.__limit : capture_frame.__sp;
  return 0;
}
extern "C" int unw_get_proc_info(unw_cursor_t *, unw_proc_info_t *info) {
  info->start_ip = capture_frame.__procedure;
  return 0;
}
#ifdef LIBC_MMIX_SHARED_STARTUP
extern "C" void __libunwind_mmix_cursor_begin(MMIXCursorLifetime *, void *) {}
extern "C" void __libunwind_mmix_cursor_begin_root(MMIXCursorLifetime *, void *) {}
extern "C" void __libunwind_mmix_cursor_end(MMIXCursorLifetime *) {}
#ifdef LIBC_MMIX_SIGNAL_CLEANUP
extern "C" int __unw_metadata_policy_enter(UnwindMetadataPolicyToken *) { return 1; }
extern "C" int __unw_metadata_policy_leave(UnwindMetadataPolicyToken *) { return 1; }
#endif
#endif

// Inspection requires signal exclusion even in profiles where registration
// itself needs no mask. Exercise that caller precondition in every profile.
class InspectionMask {
  uint64_t saved;

public:
  InspectionMask() {
    uint64_t full = UINT64_MAX;
    assert(!LIBC_NAMESPACE::syscall_impl(
        SYS_rt_sigprocmask, SIG_SETMASK, reinterpret_cast<long>(&full),
        reinterpret_cast<long>(&saved), 8));
  }
  ~InspectionMask() {
    assert(!LIBC_NAMESPACE::syscall_impl(
        SYS_rt_sigprocmask, SIG_SETMASK, reinterpret_cast<long>(&saved), 0, 8));
  }
};

static bool same_witness(const M::ActiveCleanupWitness &a,
                         const M::ActiveCleanupWitness &b) {
  const auto &x = a.ownership;
  const auto &y = b.ownership;
  return a.kind == b.kind && a.dispatch == b.dispatch && a.root.matches(b.root) &&
         M::same_link(a.executing, b.executing) &&
         M::same_link(a.remaining, b.remaining) &&
         a.registration_serial == b.registration_serial &&
         a.invocation_depth == b.invocation_depth &&
         a.callback == b.callback && a.argument == b.argument &&
         x.owner == y.owner && x.invocation == y.invocation &&
         x.record == y.record && x.serial == y.serial &&
         x.remaining == y.remaining && x.invocations == y.invocations &&
         x.lease.snapshot == y.lease.snapshot &&
         x.lease.images[0] == y.lease.images[0] &&
         x.lease.images[1] == y.lease.images[1];
}

static bool same_witness(const M::PendingCleanupWitness &a,
                         const M::PendingCleanupWitness &b) {
  const auto &x = a.root;
  const auto &y = b.root;
  return x.owner == y.owner && x.ro == y.ro && x.procedure == y.procedure &&
         x.sp == y.sp && x.chain == y.chain && x.generation == y.generation &&
         x.active == y.active && x.kind == y.kind &&
         M::same_link(a.remaining, b.remaining) &&
         a.registration_serial == b.registration_serial &&
         M::CleanupOwners::same_pending_witness(a.ownership, b.ownership);
}

static void rejected_pending(const M::PendingCleanupWitness &witness) {
  const auto before_acquired = acquired, before_released = released;
  const auto before_masks = mask_changes, before_cursors = cursor_captures;
  const auto before_scopes = scopes;
  const auto before_head = M::head;
  const auto before_serial = M::serial;
  const auto *before_active = M::active_dispatch;
  const bool before_dispatching = M::dispatching;
  auto unchanged = witness;
  const auto &root = M::current_control ? M::current_control->unwind_root
                                      : witness.root;
  assert(!M::snapshot_pending_cleanup(root, unchanged));
  assert(same_witness(unchanged, witness));
  assert(!M::validate_pending_cleanup(root, witness));
  assert(acquired == before_acquired && released == before_released);
  assert(mask_changes == before_masks && cursor_captures == before_cursors);
  assert(scopes == before_scopes && signal_mask == UINT64_MAX);
  assert(M::same_link(M::head, before_head) && M::serial == before_serial);
  assert(M::active_dispatch == before_active &&
         M::dispatching == before_dispatching);
}

static void rejected_pending() {
  M::PendingCleanupWitness sentinel;
  if (M::current_control)
    sentinel.root = M::current_control->unwind_root;
  sentinel.registration_serial = UINT64_MAX;
  sentinel.ownership.remaining =
      reinterpret_cast<const M::CleanupOwners::Node *>(1);
  rejected_pending(sentinel);
}

static void inspect(void *) {
  assert(signal_mask == 0x400);
  ++calls;
  InspectionMask mask;
  auto root = M::current_control->unwind_root;
  const unsigned before_acquired = acquired, before_released = released;
  const unsigned before_masks = mask_changes, before_cursors = cursor_captures;
  const unsigned before_scopes = scopes;
  const bool before_enabled = M::current_control->cancellation.enabled();
  auto read_only = [&] {
    assert(acquired == before_acquired && released == before_released);
    assert(mask_changes == before_masks && cursor_captures == before_cursors);
    assert(signal_mask == UINT64_MAX && scopes == before_scopes);
    assert(M::current_control->cancellation.enabled() == before_enabled);
  };
  M::ActiveCleanupWitness witness;
  rejected_pending();
  assert(!M::validate_active_c_cleanup(root, previous));
  assert(M::snapshot_active_c_cleanup(root, witness) == expect_witness);
  if (!expect_witness) {
    assert(!witness.dispatch);
    auto unchanged = previous;
    assert(!M::snapshot_active_c_cleanup(root, unchanged));
    assert(same_witness(unchanged, previous));
    read_only();
    return;
  }
  assert(witness.kind == expected_kind && witness.invocation_depth == 1);
  assert(M::dispatching == (expected_kind == M::ActiveCleanupKind::CDispatch));
  assert(M::active_dispatch);
  assert(!witness.executing.__record->__serial);
  assert(M::same_link(witness.remaining, M::head));
#ifdef LIBC_MMIX_SHARED_STARTUP
  assert(scopes == 1);
  assert(witness.ownership.invocations == 1);
#else
  const auto &ownership = witness.ownership;
  assert(!ownership.owner && !ownership.invocation && !ownership.record &&
         !ownership.serial && !ownership.remaining && !ownership.invocations &&
         !ownership.lease.snapshot && !ownership.lease.images[0] &&
         !ownership.lease.images[1]);
  assert(!acquired && !released && !scopes);
#endif
  MmixCleanupLink checkpoint{};
  if (M::dispatching) {
    assert(!M::snapshot_c_cleanup(checkpoint) && !checkpoint.__record);
    assert(!M::c_cleanup_at_checkpoint(witness.remaining));
    assert(!M::validate_fork_cleanup(root));
  } else {
    assert(M::snapshot_c_cleanup(checkpoint));
    assert(M::same_link(checkpoint, witness.remaining));
    assert(M::c_cleanup_at_checkpoint(checkpoint));
  }
  assert(M::validate_active_c_cleanup(root, witness));
  assert(M::validate_active_c_cleanup(root, witness));
  auto repeated = witness;
  assert(M::snapshot_active_c_cleanup(root, repeated));
  assert(same_witness(repeated, witness));

  for (unsigned bad = 0; bad != 23; ++bad) {
    auto changed = witness;
    switch (bad) {
    case 0: changed.dispatch = nullptr; break;
    case 1: ++changed.root.generation; break;
    case 2: changed.root.kind = M::UnwindRootKind::LastThreadProcess; break;
    case 3: ++changed.executing.__serial; break;
    case 4: changed.executing.__frame.__owner = nullptr; break;
    case 5: changed.executing.__frame.__ro += 8; break;
    case 6: changed.executing.__frame.__limit += 8; break;
    case 7: ++changed.remaining.__serial; break;
    case 8: ++changed.registration_serial; break;
    case 9: changed.callback = nullptr; break;
    case 10: changed.argument = &changed; break;
    case 11: changed.ownership.invocation =
                 reinterpret_cast<const M::CleanupOwners::Invocation *>(1); break;
    case 12: changed.ownership.lease.snapshot =
                 witness.ownership.lease.snapshot ? nullptr : &changed; break;
    case 13: ++changed.ownership.invocations; break;
    case 14: changed.ownership.owner = witness.ownership.owner ? nullptr
                 : reinterpret_cast<const M::CleanupOwners *>(1); break;
    case 15: changed.ownership.record = &changed; break;
    case 16: ++changed.ownership.serial; break;
    case 17: changed.ownership.remaining =
                 reinterpret_cast<const M::CleanupOwners::Node *>(1); break;
    case 18: changed.ownership.lease.images[0] =
                 witness.ownership.lease.images[0] ? nullptr : &changed; break;
    case 19: changed.ownership.lease.images[1] = &changed; break;
    case 20: changed.kind = M::ActiveCleanupKind::None; break;
    case 21: changed.kind = expected_kind == M::ActiveCleanupKind::CDispatch
                               ? M::ActiveCleanupKind::NormalPop
                               : M::ActiveCleanupKind::CDispatch; break;
    case 22: ++changed.invocation_depth; break;
    }
    assert(!M::validate_active_c_cleanup(root, changed));
  }
  auto rejected = [&] {
    assert(!M::validate_active_c_cleanup(root, witness));
    auto unchanged = witness;
    assert(!M::snapshot_active_c_cleanup(root, unchanged));
    assert(same_witness(unchanged, witness));
    assert(M::active_dispatch == witness.dispatch);
    rejected_pending();
  };
#ifdef LIBC_MMIX_SHARED_STARTUP
  {
    // An invalid Invocation observation is not evidence of callback absence.
    M::CleanupOwners::Invocation inactive;
    auto *activation = const_cast<M::CleanupActivation *>(M::active_dispatch);
    auto ownership = activation->entry.ownership;
    activation->entry.ownership.invocation = &inactive;
    rejected();
    activation->entry.ownership = ownership;
  }
#endif
  ++chain;
  rejected();
  --chain;
  query_failure = true;
  rejected();
  query_failure = false;
  for (unsigned bad = 0; bad != 9; ++bad) {
    auto changed = root;
    switch (bad) {
    case 0: changed.active = false; break;
    case 1: changed.owner = nullptr; break;
    case 2: changed.ro += 8; break;
    case 3: changed.sp += 8; break;
    case 4: ++changed.procedure; break;
    case 5: ++changed.chain; break;
    case 6: ++changed.generation; break;
    case 7: changed.kind = M::UnwindRootKind::TerminalThreadCleanup; break;
    case 8: changed.kind = M::UnwindRootKind::LastThreadProcess; break;
    }
    assert(!M::validate_active_c_cleanup(changed, witness));
    auto unchanged = witness;
    assert(!M::snapshot_active_c_cleanup(changed, unchanged));
    assert(same_witness(unchanged, witness));
    M::current_control->unwind_root = changed;
    rejected();
    M::current_control->unwind_root = root;
  }
  auto *control = M::current_control;
  M::current_control = nullptr;
  rejected();
  M::current_control = control;
  ++M::serial;
  if (M::dispatching) {
    rejected();
  } else {
    assert(!M::validate_active_c_cleanup(root, witness));
    M::ActiveCleanupWitness changed_serial;
    assert(M::snapshot_active_c_cleanup(root, changed_serial));
    assert(changed_serial.registration_serial == M::serial);
  }
  --M::serial;
  auto saved_head = M::head;
  ++M::head.__serial;
  rejected();
  M::head = saved_head;
#ifdef LIBC_MMIX_SIGNAL_CLEANUP
  M::SignalEntryRecord entry;
  M::signal_boundary_owner.live = &entry;
  rejected();
  M::signal_boundary_owner.live = nullptr;
#endif
  auto *record = witness.executing.__record;
  auto saved_record = *record;
  record->__serial = witness.executing.__serial;
  rejected();
  *record = saved_record;
  record->__callback = nullptr;
  rejected();
  *record = saved_record;
  record->__argument = record;
  rejected();
  *record = saved_record;
  ++record->__previous.__serial;
  rejected();
  *record = saved_record;
  if (witness.remaining.__record) {
    auto *pending = witness.remaining.__record;
    auto saved = *pending;
    ++pending->__serial;
    rejected();
    *pending = saved;
    pending->__previous = witness.remaining; // Cycle must fail, not loop.
    rejected();
    *pending = saved;
    if (saved.__previous.__record) {
      // A foreign frame below the inner stack bound must fail before reading.
      pending->__previous.__record = reinterpret_cast<MmixCleanupRecord *>(8);
      pending->__previous.__frame.__sp = 8;
      pending->__previous.__frame.__limit = 8 + sizeof(MmixCleanupRecord);
      rejected();
      *pending = saved;
      for (unsigned bad = 0; bad != 13; ++bad) {
        auto &link = pending->__previous;
        switch (bad) {
        case 0: link.__frame.__owner = nullptr; break;
        case 1: ++link.__frame.__chain; break;
        case 2: ++link.__frame.__generation; break;
        case 3: link.__frame.__ro = root.ro - 8; break;
        case 4: link.__frame.__ro += 1; break;
        case 5: link.__frame.__sp = reinterpret_cast<uintptr_t>(link.__record) + 8;
          break;
        case 6: link.__frame.__limit = reinterpret_cast<uintptr_t>(link.__record) +
                                      sizeof(*link.__record) - 8; break;
        case 7: link.__frame.__procedure = 0; break;
        case 8: link.__serial = witness.remaining.__serial; break;
        case 9: link.__record = nullptr; break; // Nonempty end sentinel.
        case 10: link.__record = reinterpret_cast<MmixCleanupRecord *>(
                     reinterpret_cast<uintptr_t>(link.__record) + 1); break;
        case 11: link.__frame.__ro += 8; break;
        case 12: link.__frame.__procedure += 8; break;
        }
        rejected();
        *pending = saved;
      }
    }
    pending->__callback = nullptr;
    rejected();
    *pending = saved;
  }
  assert(M::validate_active_c_cleanup(root, witness));
  read_only();
  previous = witness;

#ifdef LIBC_MMIX_SHARED_STARTUP
  if (copy_process) {
    copy_process = false;
    // Isolated host process-copy/accounting control, NOT target fork admission.
    // Both address spaces retain their original stack Invocation and mock slots.
    child_pid = ::fork();
    assert(child_pid >= 0);
    copied_child = child_pid == 0;
    assert(M::validate_active_c_cleanup(root, witness));
    assert(acquired == before_acquired && released == before_released);
  }
#endif
}

#ifdef LIBC_MMIX_SHARED_STARTUP
static void owner_checks() {
  M::RegistrationMask mask;
  M::CleanupOwners owners, foreign;
  M::CleanupOwners::Node node, pending;
  M::CleanupOwners::Invocation invocation, extra;
  M::CleanupOwners::Witness witness;
  int a, b;
  assert(owners.push(pending, &a, 1, 16));
  assert(owners.push(node, &b, 2, 32));
  assert(owners.detach(node, &b, 2, invocation));
  assert(owners.snapshot(invocation, witness));
  assert(owners.validate(invocation, witness));
  assert(!foreign.validate(invocation, witness));
  assert(!owners.validate(extra, witness));
  auto *cursor = witness.remaining;
  assert(M::CleanupOwners::match_record(cursor, &a, 1) && !cursor);
  // Reusing the detached Node cannot rewrite the Invocation identity or lease.
  assert(owners.push(node, &b, 3, 32));
  assert(!owners.validate(invocation, witness));
  assert(owners.detach(node, &b, 3, extra));
  assert(!owners.validate(invocation, witness));
  extra.release();
  assert(owners.validate(invocation, witness));
  invocation.release();
  auto count = released;
  invocation.release();
  assert(released == count && !owners.validate(invocation, witness));
  assert(owners.detach(pending, &a, 1, extra));
  extra.release();
  assert(owners.idle());
}
#endif

struct alignas(8) NestedPop {
  MmixCleanupRecord records[5]{};
  MmixCleanupOwner owners[5]{};
  M::ActiveCleanupWitness outer{}, inner{};
  unsigned callbacks = 0, leaf_depth = 0;
};

static M::ActiveCleanupWitness observe_pop(unsigned depth) {
  assert(signal_mask == 0x400 && !M::dispatching);
  InspectionMask mask;
  const auto before_acquired = acquired, before_released = released;
  const auto before_masks = mask_changes, before_cursors = cursor_captures;
  const auto before_scopes = scopes;
  const bool before_enabled = M::current_control->cancellation.enabled();
  M::ActiveCleanupWitness witness;
  auto root = M::current_control->unwind_root;
  rejected_pending();
  assert(M::snapshot_active_c_cleanup(root, witness));
  assert(witness.kind == M::ActiveCleanupKind::NormalPop);
  assert(witness.invocation_depth == depth &&
         witness.dispatch == M::active_dispatch);
  assert(witness.registration_serial == M::serial);
  assert(!witness.executing.__record->__serial);
  assert(M::same_link(witness.remaining, M::head));
  assert(M::validate_active_c_cleanup(root, witness));
#ifdef LIBC_MMIX_SHARED_STARTUP
  assert(scopes == depth && witness.ownership.invocations == depth);
  assert(static_cast<const Slot *>(witness.ownership.lease.snapshot)->live);
#else
  assert(M::CleanupOwners::same_witness(witness.ownership,
                                       M::CleanupOwners::Witness{}));
#endif
  assert(acquired == before_acquired && released == before_released);
  assert(mask_changes == before_masks && cursor_captures == before_cursors);
  assert(scopes == before_scopes && signal_mask == UINT64_MAX);
  assert(M::current_control->cancellation.enabled() == before_enabled);
  return witness;
}

static void stale(const M::ActiveCleanupWitness &witness) {
  InspectionMask mask;
  assert(!M::validate_active_c_cleanup(M::current_control->unwind_root, witness));
}

static void rejected_pop(const M::ActiveCleanupWitness &witness) {
  auto unchanged = witness;
  assert(!M::snapshot_active_c_cleanup(M::current_control->unwind_root,
                                      unchanged));
  assert(same_witness(unchanged, witness));
  assert(!M::validate_active_c_cleanup(M::current_control->unwind_root, witness));
  if (M::active_dispatch)
    rejected_pending();
}

static void push_nested(NestedPop &state, unsigned index,
                        void (*callback)(void *)) {
#ifdef LIBC_MMIX_ASYNC_CANCELLATION
  M::__llvm_libc_mmix_thread_cleanup_push_cpp(
      &state.records[index], &state.owners[index], callback, &state);
  assert(state.records[index].__serial & 1);
#else
  // Non-async providers do not register their C++ guards. Exercise their real
  // registered normal-pop path without inventing a C++ tag or direct hook.
  M::__llvm_libc_mmix_thread_cleanup_push(
      &state.records[index], &state.owners[index], callback, &state);
#endif
}

static void unused_callback(void *) { assert(false); }

static void forbidden_registration(void *argument) {
  auto &state = *static_cast<NestedPop *>(argument);
  assert(M::dispatching);
  push_nested(state, 1, unused_callback);
}

static void c_dispatch_registration_check(NestedPop &state) {
  // A death control, not a returning-child cleanup/recovery certificate.
  pid_t child = ::fork();
  assert(child >= 0);
  if (!child) {
    rlimit no_core{0, 0};
    assert(!setrlimit(RLIMIT_CORE, &no_core));
    M::__llvm_libc_mmix_thread_cleanup_push(
        &state.records[0], &state.owners[0], forbidden_registration, &state);
    M::dispatch_c_cleanup(capture_frame.__ro, capture_frame.__procedure,
                           capture_frame.__sp);
    _exit(1);
  }
  int status;
  assert(waitpid(child, &status, 0) == child);
  assert(WIFSIGNALED(status) &&
         (WTERMSIG(status) == SIGILL || WTERMSIG(status) == SIGTRAP));
}

static void nested_leaf(void *argument) {
  auto &state = *static_cast<NestedPop *>(argument);
  ++state.callbacks;
  auto witness = observe_pop(state.leaf_depth);
  assert(witness.executing.__record == &state.records[4]);
  stale(state.outer);
  if (state.leaf_depth == 3)
    stale(state.inner);
}

static void nested_inner(void *argument) {
  auto &state = *static_cast<NestedPop *>(argument);
  ++state.callbacks;
  state.inner = observe_pop(2);
  assert(state.inner.executing.__record == &state.records[3]);
  assert(state.inner.remaining.__record == &state.records[2]);
  stale(state.outer);
  {
    InspectionMask mask;
    // The top Invocation cannot conceal a damaged suspended outer activation.
    auto callback = state.records[1].__callback;
    state.records[1].__callback = nullptr;
    rejected_pop(state.inner);
    state.records[1].__callback = callback;
    assert(M::validate_active_c_cleanup(M::current_control->unwind_root,
                                        state.inner));
  }
  push_nested(state, 4, nested_leaf);
  stale(state.inner);
  auto with_pending = observe_pop(2);
  assert(with_pending.remaining.__record == &state.records[4]);
  assert(with_pending.registration_serial > state.inner.registration_serial);
  state.leaf_depth = 3;
  M::__llvm_libc_mmix_thread_cleanup_pop(&state.records[4], 1);
  stale(with_pending);
  stale(state.inner);
  auto restored = observe_pop(2);
  assert(restored.dispatch == state.inner.dispatch);
  assert(M::same_link(restored.remaining, state.inner.remaining));
  assert(M::CleanupOwners::same_invocation(restored.ownership,
                                          state.inner.ownership));
  state.inner = restored;
}

static void nested_outer(void *argument) {
  auto &state = *static_cast<NestedPop *>(argument);
  ++state.callbacks;
  state.outer = observe_pop(1);
  assert(state.outer.executing.__record == &state.records[1]);
  assert(state.outer.remaining.__record == &state.records[0]);
  const auto original = state.outer;
  push_nested(state, 2, unused_callback);
  stale(original);
  state.outer = observe_pop(1);
  assert(state.outer.remaining.__record == &state.records[2]);
  assert(state.outer.registration_serial > original.registration_serial);
  assert(M::CleanupOwners::same_invocation(state.outer.ownership,
                                          original.ownership));
  {
    InspectionMask mask;
    auto saved = state.records[2];
    ++state.records[2].__serial;
    rejected_pop(state.outer);
    state.records[2] = saved;
    state.records[2].__previous = M::head;
    rejected_pop(state.outer);
    state.records[2] = saved;
#ifdef LIBC_MMIX_SHARED_STARTUP
    // Pending registrations and extra detached owners are checked separately.
    M::CleanupOwners::Node foreign;
    M::CleanupOwners::Invocation extra;
    int record;
    assert(M::callback_owners.push(foreign, &record, 1000, 16));
    rejected_pop(state.outer);
    assert(M::callback_owners.detach(foreign, &record, 1000, extra));
    rejected_pop(state.outer);
    extra.release();
#endif
    assert(M::validate_active_c_cleanup(M::current_control->unwind_root,
                                        state.outer));
  }
  push_nested(state, 3, nested_inner);
  M::__llvm_libc_mmix_thread_cleanup_pop(&state.records[3], 1);
  stale(state.inner);
  stale(state.outer);
  auto restored = observe_pop(1);
  assert(restored.dispatch == original.dispatch);
  assert(restored.remaining.__record == &state.records[2]);
  assert(M::CleanupOwners::same_invocation(restored.ownership,
                                          original.ownership));
  auto before_calls = state.callbacks;
  M::__llvm_libc_mmix_thread_cleanup_pop(&state.records[2], 0);
  assert(state.callbacks == before_calls);
  stale(restored);
  restored = observe_pop(1);
  assert(M::same_link(restored.remaining, original.remaining));
  // Reuse a consumed record/Node while its enclosing callback is still active.
  push_nested(state, 4, nested_leaf);
  state.leaf_depth = 2;
  M::__llvm_libc_mmix_thread_cleanup_pop(&state.records[4], 1);
  stale(restored);
  state.outer = observe_pop(1);
  assert(state.outer.dispatch == original.dispatch);
  assert(M::same_link(state.outer.remaining, original.remaining));
  assert(M::CleanupOwners::same_invocation(state.outer.ownership,
                                          original.ownership));
}

static void nested_pop_checks() {
  auto *saved_control = M::current_control;
  const auto saved_frame = capture_frame;
  for (unsigned profile = 0; profile != 3; ++profile) {
    M::ThreadControl control;
    NestedPop state;
    M::current_control = &control;
    // No forced exception is fabricated. All three are local observations:
    // ordinary, terminal claimed, and terminal with the async type bit set.
    if (profile)
      assert(control.cancellation.claim(false));
    control.cancellation.set_async(profile == 2);
    const auto bottom = reinterpret_cast<uintptr_t>(&state);
    const auto top = bottom + sizeof(state);
    control.stack_bottom = bottom;
    control.stack_top = top;
    control.unwind_root = {&control, 0x1000, 0x1100, top, chain, 10 + profile,
                           true, M::UnwindRootKind::Thread};
    capture_frame = {&control, 0x2000, 0x2200, bottom, top, chain, 10 + profile};
    if (!profile)
      c_dispatch_registration_check(state);
    push_nested(state, 0, unused_callback);
    push_nested(state, 1, nested_outer);
    {
      InspectionMask mask;
      M::ActiveCleanupWitness absent;
      assert(!M::snapshot_active_c_cleanup(control.unwind_root, absent));
    }
    M::__llvm_libc_mmix_thread_cleanup_pop(&state.records[1], 1);
    assert(state.callbacks == 4 && !M::active_dispatch && !M::dispatching);
    assert(control.cancellation.terminal() == (profile != 0));
    assert(control.cancellation.asynchronous() == (profile == 2));
    assert(control.cancellation.enabled() == (profile == 0));
    stale(state.outer);
    stale(state.inner);
    {
      InspectionMask mask;
      rejected_pop(state.outer);
    }
    assert(M::head.__record == &state.records[0]);
#ifdef LIBC_MMIX_SHARED_STARTUP
    assert(acquired == released + 1 && !scopes);
#endif
    M::__llvm_libc_mmix_thread_cleanup_pop(&state.records[0], 0);
    assert(acquired == released && !M::has_c_cleanup());
    assert(signal_mask == 0x400);
  }
  M::current_control = saved_control;
  capture_frame = saved_frame;
}

static M::PendingCleanupWitness observe_pending() {
  InspectionMask mask;
  const auto before_acquired = acquired, before_released = released;
  const auto before_masks = mask_changes, before_cursors = cursor_captures;
  const auto before_scopes = scopes;
  const bool before_enabled = M::current_control->cancellation.enabled();
  const auto &root = M::current_control->unwind_root;
  M::PendingCleanupWitness witness;
  assert(M::snapshot_pending_cleanup(root, witness));
  assert(M::validate_pending_cleanup(root, witness));
  auto repeated = witness;
  assert(M::snapshot_pending_cleanup(root, repeated));
  assert(same_witness(witness, repeated));
  assert(M::same_link(witness.remaining, M::head));
  assert(witness.registration_serial == M::serial);
  assert(!M::active_dispatch && !M::dispatching);
#ifdef LIBC_MMIX_ASYNC_CANCELLATION
  assert(!M::async_dispatching);
#endif
#ifdef LIBC_MMIX_SHARED_STARTUP
  assert(witness.ownership.owner == &M::callback_owners);
  assert(!M::callback_owners.has_invocations());
#else
  assert(!witness.ownership.owner && !witness.ownership.remaining);
  assert(!acquired && !released);
#endif
  M::ActiveCleanupWitness absent;
  assert(!M::snapshot_active_c_cleanup(root, absent));
  assert(acquired == before_acquired && released == before_released);
  assert(mask_changes == before_masks && cursor_captures == before_cursors);
  assert(scopes == before_scopes && signal_mask == UINT64_MAX);
  assert(M::current_control->cancellation.enabled() == before_enabled);
  return witness;
}

static void direct_pending_observer(void *argument) {
  ++*static_cast<unsigned *>(argument);
  observe_pending();
}

static void pending_checks() {
  auto *saved_control = M::current_control;
  const auto saved_frame = capture_frame;
  M::ThreadControl control;
  NestedPop state;
  M::current_control = &control;
  // Observation inside C++ destruction, not a fabricated forced exception or
  // Landing certificate. Forced-state composition is independently supplied.
  assert(control.cancellation.claim(false));
  const auto bottom = reinterpret_cast<uintptr_t>(&state);
  const auto top = bottom + sizeof(state);
  control.stack_bottom = bottom;
  control.stack_top = top;
  control.unwind_root = {&control, 0x1000, 0x1100, top, chain, 20, true,
                         M::UnwindRootKind::Thread};
  capture_frame = {&control, 0x2000, 0x2200, bottom, top, chain, 20};
  const auto root = control.unwind_root;
  auto empty = observe_pending();
  assert(!empty.remaining.__record);
  push_nested(state, 0, unused_callback);
  for (unsigned i = 1; i != 3; ++i)
    M::__llvm_libc_mmix_thread_cleanup_push(
        &state.records[i], &state.owners[i], unused_callback, &state);
  auto witness = observe_pending();
#ifdef LIBC_MMIX_ASYNC_CANCELLATION
  assert(state.records[0].__serial & 1); // Pending RAII owner is not consumed.
#endif
  unsigned observations = 0;
  struct OrdinaryDestructor {
    unsigned &observations;
    ~OrdinaryDestructor() {
      ++observations;
      observe_pending();
    }
  };
  {
    OrdinaryDestructor destructor{observations};
  }
  assert(observations == 1);
#ifndef __LIBC_MMIX_REGISTER_CPP_CLEANUP
  {
    __llvm_libc_mmix_cleanup_guard guard(direct_pending_observer, &observations);
  }
  assert(observations == 2);
#endif
  {
    InspectionMask mask;
    assert(!M::validate_pending_cleanup(root, empty));
    for (unsigned bad = 0; bad != 7; ++bad) {
      auto changed = witness;
      switch (bad) {
      case 0: ++changed.registration_serial; break;
      case 1: ++changed.remaining.__serial; break;
      case 2: changed.remaining.__record = nullptr; break;
      case 3: ++changed.remaining.__frame.__limit; break;
      case 4: ++changed.root.generation; break;
      case 5: changed.ownership.owner =
                  reinterpret_cast<const M::CleanupOwners *>(1); break;
      case 6: changed.ownership.remaining =
                  reinterpret_cast<const M::CleanupOwners::Node *>(1); break;
      }
      assert(!M::validate_pending_cleanup(root, changed));
    }
    for (unsigned bad = 0; bad != 9; ++bad) {
      auto changed = root;
      switch (bad) {
      case 0: changed.active = false; break;
      case 1: changed.owner = nullptr; break;
      case 2: changed.ro += 8; break;
      case 3: changed.sp += 8; break;
      case 4: ++changed.procedure; break;
      case 5: ++changed.chain; break;
      case 6: ++changed.generation; break;
      case 7: changed.kind = M::UnwindRootKind::TerminalThreadCleanup; break;
      case 8: changed.kind = M::UnwindRootKind::LastThreadProcess; break;
      }
      auto unchanged = witness;
      assert(!M::snapshot_pending_cleanup(changed, unchanged));
      assert(same_witness(unchanged, witness));
      assert(!M::validate_pending_cleanup(changed, witness));
      control.unwind_root = changed;
      assert(!M::validate_pending_cleanup(root, witness));
      control.unwind_root = root;
    }
    ++chain;
    rejected_pending(witness);
    --chain;
    query_failure = true;
    rejected_pending(witness);
    query_failure = false;
    const auto saved_serial = M::serial;
    M::serial = 0;
    rejected_pending(witness); // Pending identifiers exceed live registration.
    M::serial = saved_serial;
    M::dispatching = true;
    rejected_pending(witness);
    M::dispatching = false;
#ifdef LIBC_MMIX_ASYNC_CANCELLATION
    M::async_dispatching = true; // Must reject even without dispatching.
    rejected_pending(witness);
    M::async_dispatching = false;
#endif
    M::CleanupActivation malformed;
    M::active_dispatch = &malformed;
    M::ActiveCleanupWitness unavailable;
    assert(!M::snapshot_active_c_cleanup(root, unavailable));
    rejected_pending(witness); // Invalid active is not absent active.
    M::active_dispatch = nullptr;
#ifdef LIBC_MMIX_SIGNAL_CLEANUP
    M::SignalEntryRecord entry;
    M::signal_boundary_owner.live = &entry;
    rejected_pending(witness);
    M::signal_boundary_owner.live = nullptr;
#endif
    const auto saved_head = M::head;
    for (unsigned bad = 0; bad != 14; ++bad) {
      switch (bad) {
      case 0: M::head.__frame.__owner = nullptr; break;
      case 1: ++M::head.__frame.__chain; break;
      case 2: ++M::head.__frame.__generation; break;
      case 3: M::head.__frame.__ro = root.ro - 8; break;
      case 4: ++M::head.__frame.__ro; break;
      case 5: M::head.__frame.__sp = top; break;
      case 6: M::head.__frame.__limit = bottom; break;
      case 7: M::head.__frame.__procedure = 0; break;
      case 8: M::head.__serial = UINT64_MAX; break;
      case 9: M::head.__record = nullptr; break;
      case 10: M::head.__record = reinterpret_cast<MmixCleanupRecord *>(8);
        M::head.__frame.__sp = 8;
        M::head.__frame.__limit = 8 + sizeof(MmixCleanupRecord); break;
      case 11: M::head.__record = reinterpret_cast<MmixCleanupRecord *>(bottom + 1);
        break;
      case 12: M::head.__frame.__limit = top + 8; break;
      case 13: M::head.__serial = 0; break;
      }
      rejected_pending(witness);
      M::head = saved_head;
    }
    // Check all pending records, including the tail, not just the top link.
    for (unsigned i = 0; i != 3; ++i) {
      auto saved = state.records[i];
      ++state.records[i].__serial;
      rejected_pending(witness);
      state.records[i] = saved;
      state.records[i].__callback = nullptr;
      rejected_pending(witness);
      state.records[i] = saved;
      state.records[i].__previous = M::head; // Cycle/nondecreasing serial.
      rejected_pending(witness);
      state.records[i] = saved;
      ++state.records[i].__previous.__frame.__generation; // Includes end sentinel.
      rejected_pending(witness);
      state.records[i] = saved;
    }
#ifdef LIBC_MMIX_SHARED_STARTUP
    M::CleanupOwners::Node foreign;
    M::CleanupOwners::Invocation extra;
    int record;
    assert(M::callback_owners.push(foreign, &record, 1000, 16));
    rejected_pending(witness); // Extra pending owner cannot be ignored.
    assert(foreign.linked());
    assert(M::callback_owners.detach(foreign, &record, 1000, extra));
    rejected_pending(witness); // No activation, but a detached owner remains.
    assert(extra.active());
    extra.release();
    // Missing owner for an otherwise well-formed pending record also refuses.
    auto *node = reinterpret_cast<M::CleanupOwners::Node *>(
        state.owners[2].__storage);
    assert(M::callback_owners.detach(*node, M::head.__record, M::head.__serial, extra));
    rejected_pending(witness);
    extra.release();
    rejected_pending(witness);
    assert(M::callback_owners.push(*node, M::head.__record, M::head.__serial + 2,
                                  reinterpret_cast<uintptr_t>(unused_callback)));
    rejected_pending(witness); // Owner serial must match, not only its record.
    assert(M::callback_owners.detach(*node, M::head.__record, M::head.__serial + 2,
                                    extra));
    extra.release();
    assert(M::callback_owners.push(*node, M::head.__record, M::head.__serial,
                                  reinterpret_cast<uintptr_t>(unused_callback)));
#endif
    assert(M::validate_pending_cleanup(root, witness));
  }
  push_nested(state, 3, unused_callback);
  {
    InspectionMask mask;
    assert(!M::validate_pending_cleanup(root, witness));
  }
  M::__llvm_libc_mmix_thread_cleanup_pop(&state.records[3], 0);
  {
    InspectionMask mask;
    assert(M::same_link(M::head, witness.remaining));
    assert(M::serial > witness.registration_serial);
    assert(!M::validate_pending_cleanup(root, witness));
  }
  observe_pending();
  for (unsigned i = 3; i != 0; --i)
    M::__llvm_libc_mmix_thread_cleanup_pop(&state.records[i - 1], 0);
  observe_pending();
  {
    InspectionMask mask;
    assert(!M::validate_pending_cleanup(root, witness));
    assert(!M::validate_pending_cleanup(root, empty)); // Empty again, newer serial.
    ++chain;
    rejected_pending(empty); // Even an empty list must authenticate the chain.
    --chain;
#ifdef LIBC_MMIX_SHARED_STARTUP
    M::CleanupOwners::Node node;
    M::CleanupOwners::Invocation detached;
    int record;
    assert(M::callback_owners.push(node, &record, 1000, 16));
    rejected_pending(empty); // Empty records must still have empty owners.
    assert(M::callback_owners.detach(node, &record, 1000, detached));
    rejected_pending(empty); // Nor may empty records hide an Invocation.
    assert(detached.active());
    detached.release();
    assert(M::callback_owners.idle());
#endif
  }
  assert(!M::head.__record && !M::active_dispatch && !M::dispatching);
  assert(acquired == released && !scopes && signal_mask == 0x400);
  assert(control.cancellation.terminal() && !control.cancellation.enabled());
  assert(!control.cancellation.asynchronous());
  M::current_control = saved_control;
  capture_frame = saved_frame;
}

static_assert(std::is_trivially_copyable<M::PendingCleanupWitness>::value);
static_assert(std::is_trivially_copyable<M::ActiveCleanupWitness>::value);
static_assert(!std::is_copy_constructible<M::CleanupOwners::Invocation>::value);

int main() {
  M::ThreadControl control;
  M::current_control = &control;
  assert(control.cancellation.claim(false));
  struct alignas(8) Storage {
    MmixCleanupRecord records[3];
    MmixCleanupOwner owners[3];
  } storage{};
  auto bottom = reinterpret_cast<uintptr_t>(&storage);
  auto top = bottom + sizeof(storage);
  control.stack_bottom = bottom;
  control.stack_top = top;
  control.unwind_root = {&control, 0x1000, 0x1100, top, chain, 7, true,
                         M::UnwindRootKind::Thread};
  capture_frame = {&control, 0x2000, 0x2200, bottom, top, chain, 7};
  auto push = [&](unsigned index, bool cpp = false) {
    M::RegistrationMask mask;
    MmixCleanupOwner *owner = nullptr;
#ifdef LIBC_MMIX_SHARED_STARTUP
    owner = &storage.owners[index];
#endif
    M::publish(&storage.records[index], owner, inspect, nullptr,
               capture_frame, cpp);
  };
  auto dispatch = [&] {
    M::dispatch_c_cleanup(capture_frame.__ro, capture_frame.__procedure,
                          capture_frame.__sp);
  };
  M::ActiveCleanupWitness empty;
  {
    InspectionMask mask;
    assert(!M::snapshot_active_c_cleanup(control.unwind_root, empty));
  }
#ifdef LIBC_MMIX_SHARED_STARTUP
  owner_checks();
#endif
  expect_witness = true;
  push(0);
  push(1);
  push(2);
#ifdef LIBC_MMIX_SHARED_STARTUP
  copy_process = true;
#endif
  dispatch();
  assert(calls == 3 && !M::has_c_cleanup() && !M::dispatching &&
         !M::active_dispatch);
  assert(acquired == released && !scopes && signal_mask == 0x400);
  {
    InspectionMask mask;
    assert(!M::validate_active_c_cleanup(control.unwind_root, previous));
    auto unchanged = previous;
    assert(!M::snapshot_active_c_cleanup(control.unwind_root, unchanged));
    assert(same_witness(unchanged, previous));
  }
  if (copied_child)
    _exit(0);
  if (child_pid) {
    int status;
    assert(waitpid(child_pid, &status, 0) == child_pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  }

  // Ordinary pop is observable without a forced-state claim. Its kind cannot
  // be substituted for C stop dispatch or independently establish Landing.
  expected_kind = M::ActiveCleanupKind::NormalPop;
  push(0);
  M::__llvm_libc_mmix_thread_cleanup_pop(&storage.records[0], 1);
  assert(calls == 4);
  push(0);
  M::__llvm_libc_mmix_thread_cleanup_pop(&storage.records[0], 0);
  assert(calls == 4 && !M::has_c_cleanup());
#ifdef LIBC_MMIX_ASYNC_CANCELLATION
  push(0, true);
  auto odd = storage.records[0].__serial;
  assert(odd & 1);
  dispatch(); // Synchronous forced unwind must leave the C++ guard owning it.
  assert(calls == 4 && storage.records[0].__serial == odd);
  M::__llvm_libc_mmix_thread_cleanup_pop(&storage.records[0], 1);
  assert(calls == 5);
  expect_witness = false;
  push(0, true);
  M::dispatch_async_cleanup(capture_frame.__ro, capture_frame.__sp);
  assert(calls == 6 && !M::has_c_cleanup());
#endif
#ifdef LIBC_MMIX_SIGNAL_CLEANUP
  M::SignalEntryRecord entry;
  entry.cleanup_tracked = true;
  entry.point.thread = &control;
  entry.point.child_chain = ++chain;
  entry.point.generation = 9;
  entry.point.wrapper.ro = capture_frame.__ro;
  entry.point.wrapper.sp = capture_frame.__sp;
  M::signal_boundary_owner.live = &entry;
  capture_frame.__chain = chain;
  capture_frame.__generation = 9;
  push(0);
  dispatch(); // Segment-local dispatch must not export an ordinary witness.
  assert(calls == 7 && !M::has_c_cleanup());
  push(0, true);
  M::__llvm_libc_mmix_thread_cleanup_pop(&storage.records[0], 1);
  assert(calls == 8 && !M::has_c_cleanup() && !M::active_dispatch);
  M::signal_boundary_owner.live = nullptr;
  --chain;
  capture_frame.__chain = chain;
  capture_frame.__generation = control.unwind_root.generation;
#endif
#ifdef LIBC_MMIX_ASYNC_CANCELLATION
  {
    auto before = calls;
    expect_witness = true;
    expected_kind = M::ActiveCleanupKind::CDispatch;
    push(0, true);
    auto pending_odd = storage.records[0].__serial;
    push(1);
    dispatch();
    assert(calls == before + 1 && storage.records[0].__serial == pending_odd);
    assert(M::head.__record == &storage.records[0] && (pending_odd & 1));
    expected_kind = M::ActiveCleanupKind::NormalPop;
    M::__llvm_libc_mmix_thread_cleanup_pop(&storage.records[0], 1);
    assert(calls == before + 2 && !M::has_c_cleanup());
  }
#endif
#ifdef LIBC_MMIX_SHARED_STARTUP
  {
    // A genuine current dispatch is insufficient when another detached owner
    // is suspended below it. Refusal must leave both owners intact.
    M::CleanupOwners::Node node;
    M::CleanupOwners::Invocation extra;
    int record;
    {
      M::RegistrationMask mask;
      assert(M::callback_owners.push(node, &record, 100, 16));
      assert(M::callback_owners.detach(node, &record, 100, extra));
    }
    auto before = calls;
    expect_witness = false;
    push(0);
    dispatch();
    assert(calls == before + 1 && extra.active() && acquired == released + 1);
    M::RegistrationMask mask;
    extra.release();
    assert(M::callback_owners.idle());
  }
#endif
  assert(acquired == released && !scopes && signal_mask == 0x400);
#ifndef LIBC_MMIX_SHARED_STARTUP
  assert(!acquired && !released);
#endif
  assert(!control.cancellation.enabled() && !M::dispatching);
  assert(!control.cancellation.asynchronous());
  assert(!M::active_dispatch);
  MmixCleanupLink checkpoint;
  assert(M::snapshot_c_cleanup(checkpoint) && !checkpoint.__record);
  assert(M::c_cleanup_at_checkpoint(checkpoint));
  nested_pop_checks();
  pending_checks();
}
