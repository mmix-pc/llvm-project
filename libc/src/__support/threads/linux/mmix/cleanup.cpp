//===-- MMIX Linux owning C cleanup stack --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "active_cleanup.h"
#include "cleanup.h"
#include "hdr/signal_macros.h"
#include "lifecycle.h"
#ifdef LIBC_MMIX_SIGNAL_CLEANUP
#include "src/signal/linux/mmix/signal_entry.h"
#endif
#ifdef LIBC_MMIX_SHARED_STARTUP
#include "callback_ownership.h"
#include "cancellation.h"
#include "cleanup_ownership.h"
#include <pthread.h>
#include "mmix/CursorLifetime.h"
#include "src/__support/CPP/new.h"
#ifdef LIBC_MMIX_SIGNAL_CLEANUP
#include "UnwindMetadataPolicy.h"
#endif
#endif
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include <asm/rstack.h>
#include <asm/unistd.h>
#include <libunwind.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {
LIBC_CONSTINIT LIBC_THREAD_LOCAL MmixCleanupLink head{};
LIBC_CONSTINIT LIBC_THREAD_LOCAL uint64_t serial = 0;
LIBC_CONSTINIT LIBC_THREAD_LOCAL bool dispatching = false;
#ifdef LIBC_MMIX_ASYNC_CANCELLATION
// Observation only: async callbacks do not publish CleanupActivations.
LIBC_CONSTINIT LIBC_THREAD_LOCAL bool async_dispatching = false;
#endif
struct CleanupActivation {
  ActiveCleanupWitness entry{};
  const CleanupActivation *previous = nullptr;
};
LIBC_CONSTINIT LIBC_THREAD_LOCAL const CleanupActivation *active_dispatch =
    nullptr;
#ifdef LIBC_MMIX_SHARED_STARTUP
LIBC_CONSTINIT LIBC_THREAD_LOCAL CleanupOwners callback_owners;
static_assert(sizeof(CleanupOwners::Node) <= sizeof(MmixCleanupOwner));
static_assert(alignof(CleanupOwners::Node) <= alignof(MmixCleanupOwner));
#endif

class RegistrationMask {
#if defined(LIBC_MMIX_ASYNC_CANCELLATION) || defined(LIBC_MMIX_SHARED_STARTUP)
  uint64_t saved;
  bool active = true;
#endif
public:
  RegistrationMask() {
#if defined(LIBC_MMIX_ASYNC_CANCELLATION) || defined(LIBC_MMIX_SHARED_STARTUP)
    uint64_t full = UINT64_MAX;
    if (syscall_impl(__NR_rt_sigprocmask, SIG_SETMASK,
                     reinterpret_cast<long>(&full),
                     reinterpret_cast<long>(&saved), 8))
      __builtin_trap();
#endif
  }
  void restore() {
#if defined(LIBC_MMIX_ASYNC_CANCELLATION) || defined(LIBC_MMIX_SHARED_STARTUP)
    if (!active)
      return;
    active = false;
    if (syscall_impl(__NR_rt_sigprocmask, SIG_SETMASK,
                     reinterpret_cast<long>(&saved), 0, 8))
      __builtin_trap();
#endif
  }
  ~RegistrationMask() { restore(); }
};

MmixCleanupFrame owner_frame(uintptr_t ro, uintptr_t procedure, uintptr_t sp) {
  auto *control = current_control;
  mmix_rstack_query query{};
  if (!control || !control->unwind_root.active ||
      control->unwind_root.owner != control || !ro || !procedure || !sp ||
      (ro & 7) || (sp & 7) ||
      syscall_impl(__NR_mmix_rstack_query, reinterpret_cast<long>(&query)))
    __builtin_trap();
  if (query.chain_id != control->unwind_root.chain) {
#if defined(LIBC_MMIX_SIGNAL_CLEANUP)
    auto *entry = current_signal_entry();
    if (!entry || !entry->cleanup_tracked ||
        entry->point.thread != control ||
        entry->point.child_chain != query.chain_id ||
        ro < entry->point.wrapper.ro || sp > entry->point.wrapper.sp)
      __builtin_trap();
    return {control, ro, procedure, sp, 0, query.chain_id,
            entry->point.generation};
#else
    // Only matched signal-boundary providers authenticate handler frames.
    __builtin_trap();
#endif
  }
  if (ro < control->unwind_root.ro ||
      (!control->retains_resources_until_process_exit() &&
       (sp < control->stack_bottom || sp >= control->stack_top)))
    __builtin_trap();
  return {control,
          ro,
          procedure,
          sp,
          0,
          query.chain_id,
          control->unwind_root.generation};
}

bool same_frame(const MmixCleanupFrame &a, const MmixCleanupFrame &b) {
  return a.owner == b.owner && a.ro == b.ro && a.procedure == b.procedure &&
         a.sp == b.sp && a.chain == b.chain && a.generation == b.generation;
}

bool same_link(const MmixCleanupLink &a, const MmixCleanupLink &b) {
  return a.record == b.record && a.serial == b.serial &&
         same_frame(a.frame, b.frame) && a.frame.limit == b.frame.limit;
}

MmixCleanupLink segment_checkpoint(const MmixCleanupFrame &frame) {
#if defined(LIBC_MMIX_SIGNAL_CLEANUP)
  if (frame.chain != current_control->unwind_root.chain) {
    auto *entry = current_signal_entry();
    if (!entry || !entry->cleanup_tracked ||
        frame.chain != entry->point.child_chain ||
        frame.generation != entry->point.generation)
      __builtin_trap();
    return entry->parent_cleanup;
  }
#else
  (void)frame;
#endif
  return {};
}

// Step past capture and push/pop to the lexical owner. Validate record storage
// against its frame interval before reading it, including on the main stack.
[[gnu::noinline, clang::disable_tail_calls]] MmixCleanupFrame
capture(MmixCleanupRecord *record) {
  unw_context_t context;
  unw_cursor_t cursor;
#ifdef LIBC_MMIX_SHARED_STARTUP
#ifdef LIBC_MMIX_SIGNAL_CLEANUP
  UnwindMetadataPolicyToken policy{};
  mmix_rstack_query query{};
  if (!current_control || !current_control->unwind_root.active ||
      syscall_impl(__NR_mmix_rstack_query, reinterpret_cast<long>(&query)))
    __builtin_trap();
  const bool handler = query.chain_id != current_control->unwind_root.chain;
  if (handler) {
    auto *entry = current_signal_entry();
    if (!entry || !entry->cleanup_tracked ||
        entry->point.child_chain != query.chain_id ||
        !__unw_metadata_policy_enter(&policy))
      __builtin_trap();
  }
#endif
  // This private cursor never escapes; do not permanently pin its image set.
  MMIXCursorLifetime lifetime{};
  __libunwind_mmix_cursor_begin(&lifetime, &cursor);
#endif
  unw_proc_info_t procedure;
  unw_word_t ro, sp, parent_sp;
  if (unw_getcontext(&context) || unw_init_local(&cursor, &context) ||
      unw_step(&cursor) != 1 || unw_is_signal_frame(&cursor) != 0 ||
      unw_step(&cursor) != 1 || unw_is_signal_frame(&cursor) != 0 ||
      unw_get_reg(&cursor, UNW_MMIX_RO, &ro) ||
      unw_get_reg(&cursor, UNW_REG_SP, &sp) ||
      unw_get_proc_info(&cursor, &procedure) || unw_step(&cursor) != 1 ||
      unw_is_signal_frame(&cursor) != 0 ||
      unw_get_reg(&cursor, UNW_REG_SP, &parent_sp))
    __builtin_trap();
  uintptr_t address = reinterpret_cast<uintptr_t>(record);
  if (address < sp || address > parent_sp ||
      parent_sp - address < sizeof(*record) || (address & 7))
    __builtin_trap();
  auto frame = owner_frame(ro, procedure.start_ip, sp);
  frame.limit = parent_sp;
#ifdef LIBC_MMIX_SHARED_STARTUP
  __libunwind_mmix_cursor_end(&lifetime);
#ifdef LIBC_MMIX_SIGNAL_CLEANUP
  if (handler && !__unw_metadata_policy_leave(&policy))
    __builtin_trap();
#endif
#endif
  return frame;
}

void validate_link(const MmixCleanupLink &link) {
  uintptr_t address = reinterpret_cast<uintptr_t>(link.record);
  if (!link.serial || !link.record || (address & 7) ||
      address < link.frame.sp || address > link.frame.limit ||
      link.frame.limit - address < sizeof(*link.record) ||
      link.record->serial != link.serial || !link.record->callback)
    __builtin_trap();
}

void invoke_callback(void (*callback)(void *), void *argument) noexcept {
  callback(argument);
}

#if defined(LIBC_MMIX_SHARED_STARTUP) && defined(LIBC_MMIX_SIGNAL_CLEANUP)
// Handler cleanup cannot enter a suspended loader transaction. Its lease uses
// the signal-safe service, but callback execution still requires the same
// disabled-state/re-enable exclusion as ordinary cleanup. All transitions are
// made under RegistrationMask; callbacks run with the caller's mask restored.
class CleanupCancellationScope {
  loader::LoaderCancellation hooks = loader_cancellation_hooks();
  uint64_t saved = 0;
  bool handler;

public:
  explicit CleanupCancellationScope(bool handler) : handler(handler) {
    if (!handler) {
      if (hooks.enter(&saved))
        __builtin_trap();
      return;
    }
    auto *control = current_control;
    if (!control)
      __builtin_trap();
    saved = control->cancellation.set_enabled(false) ? PTHREAD_CANCEL_ENABLE
                                                     : PTHREAD_CANCEL_DISABLE;
    if (control->loader_scopes.fetch_add(1) == UINT64_MAX)
      __builtin_trap();
  }
  ~CleanupCancellationScope() noexcept(false) {
    if (!handler) {
      if (hooks.leave(saved))
        __builtin_trap();
      return;
    }
    RegistrationMask mask;
    if (!current_control || !current_control->loader_scopes.fetch_sub(1) ||
        set_cancel_state(static_cast<int>(saved), nullptr))
      __builtin_trap();
  }
};
#endif

void remove(bool execute, RegistrationMask *mask = nullptr,
            bool synchronous_dispatch = false) {
  // Forced dispatch has no pop mask. Serialize both ownership lists even when
  // application signals are enabled for cleanup callbacks.
  RegistrationMask ownership_mask;
#ifdef LIBC_MMIX_SHARED_STARTUP
  // Async cancellation does not unwind this detached invocation. Keep the
  // existing cancellation suppression until its transient lease is released.
  validate_link(head);
#ifdef LIBC_MMIX_SIGNAL_CLEANUP
  CleanupCancellationScope cancellation_scope(
      head.frame.chain != current_control->unwind_root.chain);
#else
  CallbackOwnershipScope cancellation_scope;
#endif
  CleanupOwners::Invocation owner;
#endif
  CleanupActivation activation;
  auto &witness = activation.entry;
  validate_link(head);
  auto *record = head.record;
  if (record->previous.record && record->previous.serial >= head.serial)
    __builtin_trap();
  auto callback = record->callback;
  auto argument = record->argument;
  auto executing = head;
#ifdef LIBC_MMIX_SHARED_STARTUP
  if (!callback_owners.detach(record, head.serial, owner))
    __builtin_trap();
#endif
  head = record->previous;
  record->serial = 0;
  const bool observe = synchronous_dispatch || (execute && mask);
  if (observe) {
    if (synchronous_dispatch && (!dispatching || !execute || mask))
      __builtin_trap();
#ifdef LIBC_MMIX_SHARED_STARTUP
    if (!callback_owners.snapshot(owner, witness.ownership))
      __builtin_trap();
#endif
    witness.kind = synchronous_dispatch ? ActiveCleanupKind::CDispatch
                                        : ActiveCleanupKind::NormalPop;
    witness.dispatch = &activation;
    witness.root = current_control->unwind_root;
    witness.executing = executing;
    witness.remaining = head;
    witness.registration_serial = serial;
    witness.callback = callback;
    witness.argument = argument;
    // Publish after consuming the record, before restoring callback signals.
    // Shared dispatch also has a consistent owner list and stack Invocation;
    // non-image-owning dispatch leaves the ownership observation empty.
    activation.previous = active_dispatch;
    active_dispatch = &activation;
  }
  ownership_mask.restore();
  if (mask)
    mask->restore();
  if (execute)
    invoke_callback(callback, argument);
  // Normal pop restores its caller's mask before invocation. Mask again for
  // the owner-thread lease transition; detached code stayed held throughout.
  RegistrationMask release_mask;
  if (observe && current_control && current_control->cancellation.terminal() &&
      current_control->fork_recovery.is_child() &&
      !current_control->fork_recovery.allows_fork())
    __builtin_trap();
  if (observe) {
    if (active_dispatch != &activation)
      __builtin_trap();
    // No observer may borrow the Invocation while its lease is being released.
    active_dispatch = activation.previous;
  }
#ifdef LIBC_MMIX_SHARED_STARTUP
  owner.release();
#endif
}
void publish(MmixCleanupRecord *record, MmixCleanupOwner *storage,
             void (*callback)(void *), void *argument,
             const MmixCleanupFrame &frame, bool cpp_record) {
  if (!callback || dispatching || serial >= UINT64_MAX - 2)
    __builtin_trap();
  // Async profiles tag C++ records in monotonically increasing serials without
  // changing public record layout. Synchronous unwind leaves those records to
  // their RAII guard, preserving ordering with other automatic destructors.
#ifdef LIBC_MMIX_ASYNC_CANCELLATION
  uint64_t next = serial + 2;
  uint64_t identifier = next | (cpp_record ? 1 : 0);
#else
  (void)cpp_record;
  uint64_t next = serial + 1, identifier = next;
#endif
  uint64_t newer = identifier;
  auto boundary = segment_checkpoint(frame);
  for (auto link = head; !same_link(link, boundary);
       link = link.record->previous) {
    if (!link.record)
      __builtin_trap();
    if (link.record == record || link.serial >= newer ||
        link.frame.owner != frame.owner || link.frame.chain != frame.chain ||
        link.frame.generation != frame.generation || link.frame.ro > frame.ro)
      __builtin_trap();
    validate_link(link);
    newer = link.serial;
  }
#ifdef LIBC_MMIX_SHARED_STARTUP
  uintptr_t address = reinterpret_cast<uintptr_t>(storage);
  uintptr_t record_address = reinterpret_cast<uintptr_t>(record);
  if (!storage || address % alignof(MmixCleanupOwner) || address < frame.sp ||
      address > frame.limit || frame.limit - address < sizeof(*storage) ||
      !(address + sizeof(*storage) <= record_address ||
        record_address + sizeof(*record) <= address))
    __builtin_trap();
  auto *node = new (storage->__storage) CleanupOwners::Node;
  // Push has no error result: fail closed rather than publish unprotected code.
  if (!callback_owners.push(*node, record, identifier,
                            reinterpret_cast<uintptr_t>(callback)))
    __builtin_trap();
#else
  (void)storage;
#endif
  serial = next;
  *record = {head, callback, argument, identifier};
  head = {record, frame, identifier};
}
} // namespace

extern "C" [[gnu::noinline, clang::disable_tail_calls]] void
__llvm_libc_mmix_thread_cleanup_push(MmixCleanupRecord *record,
                                     MmixCleanupOwner *owner,
                                     void (*callback)(void *), void *argument) {
  RegistrationMask mask;
  auto frame = capture(record);
  publish(record, owner, callback, argument, frame, false);
}

#ifdef LIBC_MMIX_ASYNC_CANCELLATION
extern "C" [[gnu::visibility("default"), gnu::noinline,
             clang::disable_tail_calls]] void
__llvm_libc_mmix_thread_cleanup_push_cpp(MmixCleanupRecord *record,
                                        MmixCleanupOwner *owner,
                                        void (*callback)(void *), void *argument) {
  RegistrationMask mask;
  auto frame = capture(record);
  publish(record, owner, callback, argument, frame, true);
}
#endif

extern "C" [[gnu::noinline, clang::disable_tail_calls]] void
__llvm_libc_mmix_thread_cleanup_pop(MmixCleanupRecord *record, int execute) {
  RegistrationMask mask;
  auto frame = capture(record);
  if (dispatching || head.record != record || !same_frame(head.frame, frame))
    __builtin_trap();
  validate_link(head);
  remove(execute, &mask);
}

void dispatch_c_cleanup(uintptr_t ro, uintptr_t procedure, uintptr_t sp) {
  auto frame = owner_frame(ro, procedure, sp);
  if (dispatching)
    __builtin_trap();
  dispatching = true;
  auto boundary = segment_checkpoint(frame);
  while (!same_link(head, boundary)) {
    if (!head.record)
      __builtin_trap();
    if (head.frame.owner != frame.owner || head.frame.chain != frame.chain ||
        head.frame.generation != frame.generation || head.frame.ro > ro)
      __builtin_trap();
    if (head.frame.ro != ro)
      break;
    if (!same_frame(head.frame, frame))
      __builtin_trap();
#ifdef LIBC_MMIX_ASYNC_CANCELLATION
    if (head.serial & 1)
      break;
#endif
    remove(true, nullptr, true);
  }
  dispatching = false;
}

bool has_c_cleanup() { return head.record != nullptr; }

bool snapshot_c_cleanup(MmixCleanupLink &checkpoint) {
  if (dispatching)
    return false;
  checkpoint = head;
  return true;
}

bool c_cleanup_at_checkpoint(const MmixCleanupLink &checkpoint) {
  return !dispatching && same_link(head, checkpoint);
}

bool snapshot_active_c_cleanup(const UnwindRoot &root,
                               ActiveCleanupWitness &out) {
#ifdef LIBC_MMIX_ASYNC_CANCELLATION
  if (async_dispatching)
    return false;
#endif
  const auto *active = active_dispatch;
  if (!active || !current_control ||
      !root.matches(current_control->unwind_root) ||
      root.owner != current_control || root.kind != UnwindRootKind::Thread ||
      !root.chain || !root.generation || !root.ro || !root.sp ||
      !root.procedure || (root.ro & 7) || (root.sp & 7))
    return false;
  const bool c_dispatch = active->entry.kind == ActiveCleanupKind::CDispatch;
  if (c_dispatch ? (!dispatching || active->previous ||
                    active->entry.registration_serial != serial ||
                    !same_link(head, active->entry.remaining))
                 : (dispatching ||
                    active->entry.kind != ActiveCleanupKind::NormalPop))
    return false;
#ifdef LIBC_MMIX_SIGNAL_CLEANUP
  if (current_signal_entry())
    return false;
#endif
  mmix_rstack_query query{};
  if (syscall_impl(__NR_mmix_rstack_query, reinterpret_cast<long>(&query)) ||
      query.chain_id != root.chain)
    return false;

  // Validate frame bounds before reading consumed or pending record storage.
  auto frame_valid = [&](const MmixCleanupLink &link) {
    auto address = reinterpret_cast<uintptr_t>(link.record);
    const auto &frame = link.frame;
    return link.record && link.serial && !(address & 7) &&
           frame.owner == root.owner && frame.chain == root.chain &&
           frame.generation == root.generation && frame.ro >= root.ro &&
           !(frame.ro & 7) && frame.sp && !(frame.sp & 7) &&
           frame.procedure && address >= frame.sp && address <= frame.limit &&
           frame.limit - address >= sizeof(*link.record);
  };
  auto outside = [](const MmixCleanupFrame &outer,
                    const MmixCleanupFrame &inner) {
    return outer.ro <= inner.ro && outer.sp >= inner.sp &&
           (outer.ro != inner.ro ||
            (same_frame(outer, inner) && outer.limit == inner.limit));
  };

  ActiveCleanupWitness now = active->entry;
  now.remaining = head;
  now.registration_serial = serial;
  now.invocation_depth = 0;
#ifdef LIBC_MMIX_ASYNC_CANCELLATION
  const uint64_t last_identifier = serial | 1;
#else
  const uint64_t last_identifier = serial;
#endif
  uint64_t newer = UINT64_MAX;
  auto inner = active->entry.executing.frame;
  for (auto *entry = active; entry; entry = entry->previous) {
    const auto &saved = entry->entry;
    const auto &executing = saved.executing;
    if (saved.kind != now.kind || saved.dispatch != entry ||
        !root.matches(saved.root) || saved.registration_serial > serial ||
        !frame_valid(executing) || executing.serial > last_identifier ||
        executing.serial >= newer ||
        !outside(executing.frame, inner) || executing.record->serial ||
        executing.record->callback != saved.callback ||
        executing.record->argument != saved.argument ||
        !same_link(executing.record->previous, saved.remaining))
      return false;
#ifdef LIBC_MMIX_ASYNC_CANCELLATION
    if (c_dispatch && (executing.serial & 1))
      return false;
#endif
#ifdef LIBC_MMIX_SHARED_STARTUP
    CleanupOwners::Witness ownership;
    if (!saved.ownership.invocation ||
        !callback_owners.snapshot(*saved.ownership.invocation, ownership) ||
        !CleanupOwners::same_invocation(ownership, saved.ownership) ||
        ownership.record != executing.record ||
        ownership.serial != executing.serial)
      return false;
    if (entry == active)
      now.ownership = ownership;
#else
    if (!CleanupOwners::same_witness(saved.ownership, CleanupOwners::Witness{}))
      return false;
#endif
    ++now.invocation_depth;
    newer = executing.serial;
    inner = executing.frame;
  }
#ifdef LIBC_MMIX_SHARED_STARTUP
  if (now.ownership.invocations != now.invocation_depth)
    return false;
  auto *node = now.ownership.remaining;
#endif
  // Nested pop callbacks may have newer pending registrations. Each suspended
  // callback's original remaining list must still be a suffix of the live list.
  // The immutable activation chain comes from TLS, never from a saved witness.
  const auto *boundary = active;
  newer = UINT64_MAX;
  inner = head.frame;
  auto link = head;
  while (link.record) {
    if (!frame_valid(link) || link.serial > last_identifier ||
        link.serial >= newer || !outside(link.frame, inner))
      return false;
    while (boundary && same_link(link, boundary->entry.remaining)) {
      if (link.serial >= boundary->entry.executing.serial ||
          !outside(link.frame, boundary->entry.executing.frame))
        return false;
      boundary = boundary->previous;
    }
    if ((boundary &&
         (link.serial <= boundary->entry.executing.serial ||
          !outside(boundary->entry.executing.frame, link.frame))) ||
#ifdef LIBC_MMIX_SHARED_STARTUP
        !CleanupOwners::match_record(node, link.record, link.serial) ||
#endif
        link.record->serial != link.serial || !link.record->callback)
      return false;
    newer = link.serial;
    inner = link.frame;
    link = link.record->previous;
  }
#ifdef LIBC_MMIX_SHARED_STARTUP
  if (node)
    return false;
#endif
  if (!same_link(link, MmixCleanupLink{}))
    return false;
  while (boundary && same_link(link, boundary->entry.remaining))
    boundary = boundary->previous;
  if (boundary)
    return false;
  out = now;
  return true;
}

bool validate_active_c_cleanup(const UnwindRoot &root,
                               const ActiveCleanupWitness &witness) {
  ActiveCleanupWitness now;
  return snapshot_active_c_cleanup(root, now) && now.kind == witness.kind &&
         now.dispatch == witness.dispatch && now.root.matches(witness.root) &&
         same_link(now.executing, witness.executing) &&
         same_link(now.remaining, witness.remaining) &&
         now.registration_serial == witness.registration_serial &&
         now.invocation_depth == witness.invocation_depth &&
         now.callback == witness.callback && now.argument == witness.argument &&
         CleanupOwners::same_witness(now.ownership, witness.ownership);
}

bool snapshot_pending_cleanup(const UnwindRoot &root,
                              PendingCleanupWitness &out) {
  // Do not infer absence from snapshot_active_c_cleanup: a malformed active
  // callback or detached owner must not become a pending-only observation.
  if (active_dispatch || dispatching || !current_control ||
      !root.matches(current_control->unwind_root) ||
      root.owner != current_control || root.kind != UnwindRootKind::Thread ||
      !root.chain || !root.generation || !root.ro || !root.sp ||
      !root.procedure || (root.ro & 7) || (root.sp & 7))
    return false;
#ifdef LIBC_MMIX_ASYNC_CANCELLATION
  if (async_dispatching)
    return false;
#endif
#ifdef LIBC_MMIX_SIGNAL_CLEANUP
  if (current_signal_entry())
    return false;
#endif
  mmix_rstack_query query{};
  if (syscall_impl(__NR_mmix_rstack_query, reinterpret_cast<long>(&query)) ||
      query.chain_id != root.chain)
    return false;

  PendingCleanupWitness now;
  now.root = root;
  now.remaining = head;
  now.registration_serial = serial;
#ifdef LIBC_MMIX_SHARED_STARTUP
  if (!callback_owners.snapshot_pending(now.ownership))
    return false;
  auto *node = now.ownership.remaining;
#endif
#ifdef LIBC_MMIX_ASYNC_CANCELLATION
  const uint64_t last_identifier = serial | 1;
#else
  const uint64_t last_identifier = serial;
#endif
  uint64_t newer = UINT64_MAX;
  auto inner = head.frame;
  auto link = head;
  while (link.record) {
    auto address = reinterpret_cast<uintptr_t>(link.record);
    const auto &frame = link.frame;
    // Check storage and frame ordering before dereferencing any record.
    if (!link.serial || link.serial > last_identifier || link.serial >= newer ||
        (address & 7) || frame.owner != root.owner ||
        frame.chain != root.chain || frame.generation != root.generation ||
        frame.ro < root.ro || (frame.ro & 7) || !frame.sp || (frame.sp & 7) ||
        !frame.procedure || address < frame.sp || address > frame.limit ||
        frame.limit - address < sizeof(*link.record) || frame.limit > root.sp ||
        (!current_control->retains_resources_until_process_exit() &&
         (frame.sp < current_control->stack_bottom ||
          frame.limit > current_control->stack_top)) ||
        frame.ro > inner.ro || frame.sp < inner.sp ||
        (frame.ro == inner.ro &&
         (!same_frame(frame, inner) || frame.limit != inner.limit)))
      return false;
    if (
#ifdef LIBC_MMIX_SHARED_STARTUP
        !CleanupOwners::match_record(node, link.record, link.serial) ||
#endif
        link.record->serial != link.serial || !link.record->callback)
      return false;
    newer = link.serial;
    inner = frame;
    link = link.record->previous;
  }
#ifdef LIBC_MMIX_SHARED_STARTUP
  if (node)
    return false;
#endif
  if (!same_link(link, MmixCleanupLink{}))
    return false;
  out = now;
  return true;
}

bool validate_pending_cleanup(const UnwindRoot &root,
                              const PendingCleanupWitness &witness) {
  PendingCleanupWitness now;
  return snapshot_pending_cleanup(root, now) && now.root.matches(witness.root) &&
         same_link(now.remaining, witness.remaining) &&
         now.registration_serial == witness.registration_serial &&
         CleanupOwners::same_pending_witness(now.ownership, witness.ownership);
}

bool validate_fork_cleanup(const UnwindRoot &root) {
  if (dispatching || !root.ro || !root.sp || (root.ro & 7) || (root.sp & 7))
    return false;
  unw_context_t context;
  unw_cursor_t cursor;
#ifdef LIBC_MMIX_SHARED_STARTUP
  MMIXCursorLifetime lifetime{};
  __libunwind_mmix_cursor_begin(&lifetime, &cursor);
#endif
  bool valid = false;
  auto link = head;
  uint64_t newer = UINT64_MAX;
  if (!unw_getcontext(&context) && !unw_init_local(&cursor, &context)) {
    for (;;) {
      unw_word_t ro, sp;
      unw_proc_info_t procedure;
      if (unw_is_signal_frame(&cursor) != 0 ||
          unw_get_reg(&cursor, UNW_MMIX_RO, &ro) ||
          unw_get_reg(&cursor, UNW_REG_SP, &sp) ||
          unw_get_proc_info(&cursor, &procedure) || ro < root.ro)
        break;
      while (link.record && link.frame.ro >= ro) {
        if (link.serial >= newer || link.frame.owner != root.owner ||
            link.frame.chain != root.chain ||
            link.frame.generation != root.generation || link.frame.ro != ro ||
            link.frame.sp != sp || link.frame.procedure != procedure.start_ip)
          goto done;
        validate_link(link);
        newer = link.serial;
        link = link.record->previous;
      }
      if (ro == root.ro) {
        valid = !link.record && sp == root.sp &&
                procedure.start_ip == root.procedure;
        break;
      }
      if (unw_step(&cursor) != 1)
        break;
    }
  }
done:
#ifdef LIBC_MMIX_SHARED_STARTUP
  __libunwind_mmix_cursor_end(&lifetime);
#endif
  return valid;
}

#ifdef LIBC_MMIX_ASYNC_CANCELLATION
void dispatch_async_cleanup(uintptr_t interrupted_ro,
                            uintptr_t interrupted_sp) {
  auto *control = current_control;
  mmix_rstack_query query{};
  if (!control || dispatching || !control->cancellation.terminal() ||
      !control->unwind_root.active ||
      syscall_impl(__NR_mmix_rstack_query, reinterpret_cast<long>(&query)) ||
      query.chain_id != control->unwind_root.chain)
    __builtin_trap();
  dispatching = true;
  async_dispatching = true;
  uint64_t newer = UINT64_MAX;
  uintptr_t inner_ro = interrupted_ro;
  while (head.record) {
    if (head.frame.owner != control || head.frame.chain != query.chain_id ||
        head.frame.generation != control->unwind_root.generation ||
        head.frame.ro < control->unwind_root.ro ||
        head.frame.ro > interrupted_ro || head.frame.sp < interrupted_sp ||
        head.serial >= newer || head.frame.ro > inner_ro)
      __builtin_trap();
    newer = head.serial;
    inner_ro = head.frame.ro;
    remove(true);
  }
  async_dispatching = false;
  dispatching = false;
}
#endif
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
