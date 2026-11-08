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
  return a.__owner == b.__owner && a.__ro == b.__ro && a.__procedure == b.__procedure &&
         a.__sp == b.__sp && a.__chain == b.__chain && a.__generation == b.__generation;
}

bool same_link(const MmixCleanupLink &a, const MmixCleanupLink &b) {
  return a.__record == b.__record && a.__serial == b.__serial &&
         same_frame(a.__frame, b.__frame) && a.__frame.__limit == b.__frame.__limit;
}

MmixCleanupLink segment_checkpoint(const MmixCleanupFrame &frame) {
#if defined(LIBC_MMIX_SIGNAL_CLEANUP)
  if (frame.__chain != current_control->unwind_root.chain) {
    auto *entry = current_signal_entry();
    if (!entry || !entry->cleanup_tracked ||
        frame.__chain != entry->point.child_chain ||
        frame.__generation != entry->point.generation)
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
  frame.__limit = parent_sp;
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
  uintptr_t address = reinterpret_cast<uintptr_t>(link.__record);
  if (!link.__serial || !link.__record || (address & 7) ||
      address < link.__frame.__sp || address > link.__frame.__limit ||
      link.__frame.__limit - address < sizeof(*link.__record) ||
      link.__record->__serial != link.__serial || !link.__record->__callback)
    __builtin_trap();
}

void invoke_callback(void (*callback)(void *), void *argument) noexcept {
  callback(argument);
}

#if defined(LIBC_MMIX_SHARED_STARTUP) && defined(LIBC_MMIX_SIGNAL_CLEANUP)
// Handler cleanup cannot enter a suspended loader transaction. Its lease uses
// the signal-safe service, but callback execution still requires the same
// private delivery guard as ordinary cleanup. All transitions are
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
      head.__frame.__chain != current_control->unwind_root.chain);
#else
  CallbackOwnershipScope cancellation_scope;
#endif
  CleanupOwners::Invocation owner;
#endif
  CleanupActivation activation;
  auto &witness = activation.entry;
  validate_link(head);
  auto *record = head.__record;
  if (record->__previous.__record && record->__previous.__serial >= head.__serial)
    __builtin_trap();
  auto callback = record->__callback;
  auto argument = record->__argument;
  auto executing = head;
#ifdef LIBC_MMIX_SHARED_STARTUP
  if (!callback_owners.detach(record, head.__serial, owner))
    __builtin_trap();
#endif
  head = record->__previous;
  record->__serial = 0;
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
       link = link.__record->__previous) {
    if (!link.__record)
      __builtin_trap();
    if (link.__record == record || link.__serial >= newer ||
        link.__frame.__owner != frame.__owner || link.__frame.__chain != frame.__chain ||
        link.__frame.__generation != frame.__generation || link.__frame.__ro > frame.__ro)
      __builtin_trap();
    validate_link(link);
    newer = link.__serial;
  }
#ifdef LIBC_MMIX_SHARED_STARTUP
  uintptr_t address = reinterpret_cast<uintptr_t>(storage);
  uintptr_t record_address = reinterpret_cast<uintptr_t>(record);
  if (!storage || address % alignof(MmixCleanupOwner) || address < frame.__sp ||
      address > frame.__limit || frame.__limit - address < sizeof(*storage) ||
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
  if (dispatching || head.__record != record || !same_frame(head.__frame, frame))
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
    if (!head.__record)
      __builtin_trap();
    if (head.__frame.__owner != frame.__owner || head.__frame.__chain != frame.__chain ||
        head.__frame.__generation != frame.__generation || head.__frame.__ro > ro)
      __builtin_trap();
    if (head.__frame.__ro != ro)
      break;
    if (!same_frame(head.__frame, frame))
      __builtin_trap();
#ifdef LIBC_MMIX_ASYNC_CANCELLATION
    if (head.__serial & 1)
      break;
#endif
    remove(true, nullptr, true);
  }
  dispatching = false;
}

bool has_c_cleanup() { return head.__record != nullptr; }

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
    auto address = reinterpret_cast<uintptr_t>(link.__record);
    const auto &frame = link.__frame;
    return link.__record && link.__serial && !(address & 7) &&
           frame.__owner == root.owner && frame.__chain == root.chain &&
           frame.__generation == root.generation && frame.__ro >= root.ro &&
           !(frame.__ro & 7) && frame.__sp && !(frame.__sp & 7) &&
           frame.__procedure && address >= frame.__sp && address <= frame.__limit &&
           frame.__limit - address >= sizeof(*link.__record);
  };
  auto outside = [](const MmixCleanupFrame &outer,
                    const MmixCleanupFrame &inner) {
    return outer.__ro <= inner.__ro && outer.__sp >= inner.__sp &&
           (outer.__ro != inner.__ro ||
            (same_frame(outer, inner) && outer.__limit == inner.__limit));
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
  auto inner = active->entry.executing.__frame;
  for (auto *entry = active; entry; entry = entry->previous) {
    const auto &saved = entry->entry;
    const auto &executing = saved.executing;
    if (saved.kind != now.kind || saved.dispatch != entry ||
        !root.matches(saved.root) || saved.registration_serial > serial ||
        !frame_valid(executing) || executing.__serial > last_identifier ||
        executing.__serial >= newer ||
        !outside(executing.__frame, inner) || executing.__record->__serial ||
        executing.__record->__callback != saved.callback ||
        executing.__record->__argument != saved.argument ||
        !same_link(executing.__record->__previous, saved.remaining))
      return false;
#ifdef LIBC_MMIX_ASYNC_CANCELLATION
    if (c_dispatch && (executing.__serial & 1))
      return false;
#endif
#ifdef LIBC_MMIX_SHARED_STARTUP
    CleanupOwners::Witness ownership;
    if (!saved.ownership.invocation ||
        !callback_owners.snapshot(*saved.ownership.invocation, ownership) ||
        !CleanupOwners::same_invocation(ownership, saved.ownership) ||
        ownership.record != executing.__record ||
        ownership.serial != executing.__serial)
      return false;
    if (entry == active)
      now.ownership = ownership;
#else
    if (!CleanupOwners::same_witness(saved.ownership, CleanupOwners::Witness{}))
      return false;
#endif
    ++now.invocation_depth;
    newer = executing.__serial;
    inner = executing.__frame;
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
  inner = head.__frame;
  auto link = head;
  while (link.__record) {
    if (!frame_valid(link) || link.__serial > last_identifier ||
        link.__serial >= newer || !outside(link.__frame, inner))
      return false;
    while (boundary && same_link(link, boundary->entry.remaining)) {
      if (link.__serial >= boundary->entry.executing.__serial ||
          !outside(link.__frame, boundary->entry.executing.__frame))
        return false;
      boundary = boundary->previous;
    }
    if ((boundary &&
         (link.__serial <= boundary->entry.executing.__serial ||
          !outside(boundary->entry.executing.__frame, link.__frame))) ||
#ifdef LIBC_MMIX_SHARED_STARTUP
        !CleanupOwners::match_record(node, link.__record, link.__serial) ||
#endif
        link.__record->__serial != link.__serial || !link.__record->__callback)
      return false;
    newer = link.__serial;
    inner = link.__frame;
    link = link.__record->__previous;
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
  auto inner = head.__frame;
  auto link = head;
  while (link.__record) {
    auto address = reinterpret_cast<uintptr_t>(link.__record);
    const auto &frame = link.__frame;
    // Check storage and frame ordering before dereferencing any record.
    if (!link.__serial || link.__serial > last_identifier || link.__serial >= newer ||
        (address & 7) || frame.__owner != root.owner ||
        frame.__chain != root.chain || frame.__generation != root.generation ||
        frame.__ro < root.ro || (frame.__ro & 7) || !frame.__sp || (frame.__sp & 7) ||
        !frame.__procedure || address < frame.__sp || address > frame.__limit ||
        frame.__limit - address < sizeof(*link.__record) || frame.__limit > root.sp ||
        (!current_control->retains_resources_until_process_exit() &&
         (frame.__sp < current_control->stack_bottom ||
          frame.__limit > current_control->stack_top)) ||
        frame.__ro > inner.__ro || frame.__sp < inner.__sp ||
        (frame.__ro == inner.__ro &&
         (!same_frame(frame, inner) || frame.__limit != inner.__limit)))
      return false;
    if (
#ifdef LIBC_MMIX_SHARED_STARTUP
        !CleanupOwners::match_record(node, link.__record, link.__serial) ||
#endif
        link.__record->__serial != link.__serial || !link.__record->__callback)
      return false;
    newer = link.__serial;
    inner = frame;
    link = link.__record->__previous;
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
      while (link.__record && link.__frame.__ro >= ro) {
        if (link.__serial >= newer || link.__frame.__owner != root.owner ||
            link.__frame.__chain != root.chain ||
            link.__frame.__generation != root.generation || link.__frame.__ro != ro ||
            link.__frame.__sp != sp || link.__frame.__procedure != procedure.start_ip)
          goto done;
        validate_link(link);
        newer = link.__serial;
        link = link.__record->__previous;
      }
      if (ro == root.ro) {
        valid = !link.__record && sp == root.sp &&
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
  while (head.__record) {
    if (head.__frame.__owner != control || head.__frame.__chain != query.chain_id ||
        head.__frame.__generation != control->unwind_root.generation ||
        head.__frame.__ro < control->unwind_root.ro ||
        head.__frame.__ro > interrupted_ro || head.__frame.__sp < interrupted_sp ||
        head.__serial >= newer || head.__frame.__ro > inner_ro)
      __builtin_trap();
    newer = head.__serial;
    inner_ro = head.__frame.__ro;
    remove(true);
  }
  async_dispatching = false;
  dispatching = false;
}
#endif
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
