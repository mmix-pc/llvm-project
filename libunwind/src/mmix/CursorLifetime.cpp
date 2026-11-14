//===-- MMIX private cursor ownership -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CursorLifetime.h"

namespace {
// Both startup providers and static opt-in use preallocated TLS. Lifetime
// tracking must not enter a dynamic TLS allocator.
thread_local MMIXCursorLifetime *owners __attribute__((tls_model("initial-exec")));
thread_local void *query __attribute__((tls_model("initial-exec")));
thread_local uintptr_t managed_stop __attribute__((tls_model("initial-exec")));

enum ForcedPhase : unsigned {
  Empty,
  Walking,
  Stopped,
  Landing,
  Changing,
  Quarantined
};
struct ForcedContinuation {
  void *exception;
  uintptr_t stop;
  void *argument;
  void *cursor;
  MMIXCursorLifetime *base;
  MMIXCursorLifetime held;
  // MMIX compare-and-swap operates on naturally aligned octabytes.
  uintptr_t phase;
};
static_assert(__atomic_always_lock_free(sizeof(uintptr_t), nullptr),
              "forced continuation transitions must be lock-free");
thread_local ForcedContinuation forced
    __attribute__((tls_model("initial-exec")));

unsigned phase() {
  return static_cast<unsigned>(
      __atomic_load_n(&forced.phase, __ATOMIC_ACQUIRE));
}
void publish(unsigned next) {
  __atomic_store_n(&forced.phase, next, __ATOMIC_RELEASE);
}
// Do not return an unwind failure for the copied continuation: its diagnostic
// path may allocate or release ownership in the restricted fork child.
void check_quarantined_exception(void *exception) {
  if (phase() == Quarantined && forced.exception == exception)
    __builtin_trap();
}
void check_quarantined_cursor(const void *cursor) {
  if (cursor && phase() == Quarantined && forced.cursor == cursor)
    __builtin_trap();
}
bool matches(void *exception, uintptr_t stop, void *argument) {
  return forced.exception == exception && forced.stop == stop &&
         forced.argument == argument;
}
void clear_forced() {
  forced.exception = nullptr;
  forced.stop = 0;
  forced.argument = nullptr;
  forced.cursor = nullptr;
  forced.base = nullptr;
  forced.held = {};
  publish(Empty);
}

MMIXCursorLifetime *find(const void *cursor) {
  for (auto *owner = owners; owner; owner = owner->previous)
    if (owner->cursor == cursor)
      return owner;
  return nullptr;
}
void release(MMIXCursorLifetime &owner, bool escape) {
  if (owner.root_image && (owner.token || owner.fallback_token))
    __builtin_trap();
  if (phase() == Quarantined &&
      (&owner == &forced.held ||
       (owner.cursor && owner.cursor == forced.cursor)))
    __builtin_trap();
#if defined(_LIBUNWIND_MMIX_STARTUP_IMAGES) ||                                  \
    !defined(_LIBUNWIND_MMIX_CURSOR_LIFETIME)
  auto consume = [escape](uintptr_t &token) {
    if (token) {
      bool ok = escape ? __libunwind_mmix_pin_image(token)
                       : __libunwind_mmix_drop_image(token);
      if (!ok)
        __builtin_trap();
      token = 0;
    }
  };
  consume(owner.token);
  consume(owner.fallback_token);
#else
  (void)escape;
  // Static metadata is discovered by Linux.h, never through an image lease.
  if (owner.token || owner.fallback_token)
    __builtin_trap();
#endif
}
void remove(MMIXCursorLifetime &owner) {
  auto **link = &owners;
  while (*link && *link != &owner)
    link = &(*link)->previous;
  if (!*link)
    __builtin_trap();
  *link = owner.previous;
  owner = {};
}
} // namespace

extern "C" __attribute__((visibility("protected"))) void
__libunwind_mmix_cursor_begin(MMIXCursorLifetime *owner, void *cursor) {
  check_quarantined_cursor(cursor);
  if (!owner || !cursor || find(cursor))
    __builtin_trap();
  *owner = {cursor, 0, owners, 0, false};
  owners = owner;
}
extern "C" __attribute__((visibility("protected"))) void
__libunwind_mmix_cursor_begin_root(MMIXCursorLifetime *owner, void *cursor) {
  __libunwind_mmix_cursor_begin(owner, cursor);
  owner->root_image = true;
}
extern "C" __attribute__((visibility("protected"))) void
__libunwind_mmix_set_managed_stop(uintptr_t stop) {
  if (!stop || (managed_stop && managed_stop != stop))
    __builtin_trap();
  managed_stop = stop;
}
extern "C" void
__libunwind_mmix_cursor_begin_with_stop(MMIXCursorLifetime *owner, void *cursor,
                                        uintptr_t stop) {
  // Public stop callbacks may longjmp without releasing a private owner.
  // Only the libc root protocol promises an explicit terminal finish hook.
  if (!stop || stop == managed_stop)
    __libunwind_mmix_cursor_begin(owner, cursor);
  else
    *owner = {};
}
extern "C" __attribute__((visibility("protected"))) void
__libunwind_mmix_cursor_end(MMIXCursorLifetime *owner) {
  check_quarantined_cursor(owner->cursor);
  if (owner->cursor) {
    bool terminal = forced.cursor == owner->cursor;
    if (terminal) {
      if (phase() != Walking && phase() != Stopped)
        __builtin_trap();
      publish(Changing);
    }
    release(*owner, false);
    remove(*owner);
    if (terminal)
      clear_forced();
  }
}
extern "C" __attribute__((visibility("protected"))) void
__libunwind_mmix_cursor_finish(void *cursor) {
  check_quarantined_cursor(cursor);
  if (auto *owner = find(cursor))
    __libunwind_mmix_cursor_end(owner);
}
extern "C" __attribute__((visibility("protected"))) void
__libunwind_mmix_cursor_checkpoint(MMIXCursorCheckpoint *checkpoint) {
  if (!checkpoint)
    __builtin_trap();
  unsigned current = phase();
  *checkpoint = {reinterpret_cast<uintptr_t>(&owners), owners, query,
                 current == Changing ? nullptr : forced.exception, current};
}
extern "C" __attribute__((visibility("protected"))) int
__libunwind_mmix_cursor_at_checkpoint(const MMIXCursorCheckpoint *checkpoint) {
  unsigned current = phase();
  // A handler may interrupt a transition. Its checkpoint is only a witness
  // that its suspended parent did not change, never an admission certificate.
  return checkpoint &&
         checkpoint->thread == reinterpret_cast<uintptr_t>(&owners) &&
         checkpoint->owners == owners && checkpoint->query == query &&
         checkpoint->forced_exception ==
             (current == Changing ? nullptr : forced.exception) &&
         checkpoint->forced_phase == current;
}
extern "C" __attribute__((visibility("protected"))) int
__libunwind_mmix_cursor_finish_at(void *cursor,
                                 const MMIXCursorCheckpoint *checkpoint) {
  check_quarantined_cursor(cursor);
  if (!checkpoint || checkpoint->thread != reinterpret_cast<uintptr_t>(&owners) ||
      !cursor || !owners || owners->cursor != cursor ||
      owners->previous != checkpoint->owners || query != checkpoint->query ||
      query == cursor || phase() == Changing ||
      (forced.cursor == cursor
           ? checkpoint->forced_exception != nullptr ||
                 checkpoint->forced_phase != Empty
           : checkpoint->forced_exception != forced.exception ||
                 checkpoint->forced_phase != phase()))
    return 0;
  __libunwind_mmix_cursor_end(owners);
  if (!__libunwind_mmix_cursor_at_checkpoint(checkpoint))
    __builtin_trap();
  return 1;
}

extern "C" __attribute__((visibility("protected"))) int
__libunwind_mmix_forced_capture(const MMIXCursorCheckpoint *base,
                               void *exception, uintptr_t stop, void *argument,
                               MMIXForcedCheckpoint *output) {
  unsigned current = phase();
  if (!base || !output ||
      base->thread != reinterpret_cast<uintptr_t>(&owners) || base->owners ||
      base->query || base->forced_exception || base->forced_phase != Empty ||
      !exception || !stop || stop != managed_stop ||
      (current != Stopped && current != Landing) ||
      !matches(exception, stop, argument) || query || forced.base != base->owners ||
      forced.held.cursor || forced.held.previous)
    return 0;

  MMIXForcedCheckpoint result{};
  result.thread = base->thread;
  result.exception = exception;
  result.stop = stop;
  result.argument = argument;
  result.base = forced.base;
  if (current == Stopped) {
    // Only the live TLS head is dereferenced, never a borrowed checkpoint node.
    if (!forced.cursor || !owners || owners->cursor != forced.cursor ||
        owners->previous != forced.base || forced.held.token ||
        forced.held.fallback_token)
      return 0;
    result.owner = owners;
    result.cursor = forced.cursor;
    result.token = owners->token;
    result.fallback_token = owners->fallback_token;
    result.phase = MMIXForcedStopped;
  } else {
    // Landing is published before __unw_resume retires its old stack sidecar.
    // That brief, still-owned interval is not an observable cleanup boundary.
    if (forced.cursor || owners != forced.base)
      return 0;
    result.held_token = forced.held.token;
    result.held_fallback_token = forced.held.fallback_token;
    result.phase = MMIXForcedLanding;
  }
  *output = result;
  return 1;
}

extern "C" __attribute__((visibility("protected"))) int
__libunwind_mmix_forced_validate(const MMIXCursorCheckpoint *base,
                                void *exception, uintptr_t stop, void *argument,
                                const MMIXForcedCheckpoint *saved) {
  MMIXForcedCheckpoint current;
  if (!saved || !__libunwind_mmix_forced_capture(base, exception, stop, argument,
                                               &current))
    return 0;
  return saved->thread == current.thread &&
         saved->exception == current.exception && saved->stop == current.stop &&
         saved->argument == current.argument && saved->base == current.base &&
         saved->owner == current.owner && saved->cursor == current.cursor &&
         saved->token == current.token &&
         saved->fallback_token == current.fallback_token &&
         saved->held_token == current.held_token &&
         saved->held_fallback_token == current.held_fallback_token &&
         saved->phase == current.phase;
}

extern "C" __attribute__((visibility("protected"))) int
__libunwind_mmix_forced_quarantine(const MMIXCursorCheckpoint *base,
                                  void *exception, uintptr_t stop,
                                  void *argument,
                                  const MMIXForcedCheckpoint *saved) {
  if (!__libunwind_mmix_forced_validate(base, exception, stop, argument, saved))
    return 0;
  uintptr_t expected = saved->phase == MMIXForcedStopped ? Stopped : Landing;
  return __atomic_compare_exchange_n(&forced.phase, &expected, Quarantined,
                                     false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

extern "C" void __libunwind_mmix_cursor_escape(void *cursor) {
  check_quarantined_cursor(cursor);
  if (auto *owner = find(cursor)) {
    if (forced.cursor == cursor)
      __builtin_trap();
    // A raw resume/escaping metadata pointer has no observable final owner.
    // Keep its generation resident, but consume the finite query slot. Never
    // leave a TLS link to the stack frame that jumpto is about to discard.
    release(*owner, true);
    remove(*owner);
  }
}
extern "C" void __libunwind_mmix_cursor_reset(void *cursor) {
  check_quarantined_cursor(cursor);
  if (cursor && forced.cursor == cursor)
    __builtin_trap();
  if (auto *owner = find(cursor))
    release(*owner, false);
}
extern "C" int __libunwind_mmix_cursor_copy(void *to, const void *from) {
  check_quarantined_cursor(from);
  check_quarantined_cursor(to);
  auto *source = find(from);
  auto *destination = find(to);
  if (!source || !destination || source == destination || forced.cursor == to ||
      source->root_image || destination->root_image)
    return 0;
#if defined(_LIBUNWIND_MMIX_STARTUP_IMAGES) ||                                  \
    !defined(_LIBUNWIND_MMIX_CURSOR_LIFETIME)
  MMIXUnwindImage copy{};
  if (source->token && !__libunwind_mmix_retain_image(source->token, &copy))
    return 0;
  MMIXUnwindImage fallback{};
  if (source->fallback_token &&
      !__libunwind_mmix_retain_image(source->fallback_token, &fallback)) {
    if (copy.token && !__libunwind_mmix_drop_image(copy.token))
      __builtin_trap();
    return 0;
  }
  release(*destination, false);
  destination->token = copy.token;
  destination->fallback_token = fallback.token;
#else
  if (source->token || source->fallback_token)
    __builtin_trap();
  release(*destination, false);
#endif
  return 1;
}

extern "C" int __libunwind_mmix_forced_resume(void *exception, uintptr_t stop,
                                              void *argument) {
  check_quarantined_exception(exception);
  if (forced.exception && forced.exception == exception)
    return phase() == Landing && matches(exception, stop, argument) && !query;
  return !stop || stop != managed_stop;
}

extern "C" int __libunwind_mmix_forced_enter(void *cursor, void *exception,
                                             uintptr_t stop, void *argument) {
  check_quarantined_exception(exception);
  check_quarantined_cursor(cursor);
  if (!stop || stop != managed_stop)
    return !forced.exception || forced.exception != exception;
  auto *owner = find(cursor);
  unsigned current = phase();
  if (!exception || !owner || owners != owner || query || owner->token ||
      owner->fallback_token || owner->root_image ||
      (current != Empty &&
       (current != Landing || !matches(exception, stop, argument) ||
        owner->previous != forced.base)))
    return 0;
  // Changing is not admissible for another forced unwind. A signal handler may
  // only witness and return to the unchanged, suspended transition.
  publish(Changing);
  if (current == Empty) {
    forced.exception = exception;
    forced.stop = stop;
    forced.argument = argument;
    forced.base = owner->previous;
  } else {
    owner->token = forced.held.token;
    forced.held.token = 0;
    owner->fallback_token = forced.held.fallback_token;
    forced.held.fallback_token = 0;
  }
  forced.cursor = cursor;
  publish(Walking);
  return 1;
}

extern "C" int __libunwind_mmix_forced_stop(void *cursor, void *exception,
                                            uintptr_t stop, void *argument,
                                            int entering) {
  check_quarantined_exception(exception);
  check_quarantined_cursor(cursor);
  if (!stop || stop != managed_stop)
    return !forced.exception || forced.exception != exception;
  if (phase() != (entering ? Walking : Stopped) ||
      !matches(exception, stop, argument) || forced.cursor != cursor ||
      !owners || owners->cursor != cursor || query)
    return 0;
  publish(entering ? Stopped : Walking);
  return 1;
}

extern "C" int __libunwind_mmix_forced_landing(void *cursor, void *exception,
                                               uintptr_t stop, void *argument) {
  check_quarantined_exception(exception);
  check_quarantined_cursor(cursor);
  if (!stop || stop != managed_stop)
    return !forced.exception || forced.exception != exception;
  auto *owner = find(cursor);
  if (phase() != Walking || !matches(exception, stop, argument) ||
      forced.cursor != cursor || !owner || owners != owner || query ||
      owner->previous != forced.base || forced.held.token ||
      forced.held.fallback_token)
    return 0;
  publish(Changing);
  forced.held.token = owner->token;
  owner->token = 0;
  forced.held.fallback_token = owner->fallback_token;
  owner->fallback_token = 0;
  forced.cursor = nullptr;
  publish(Landing);
  // __unw_resume still retires the old stack sidecar before jumping. The TLS
  // owner, not that discarded activation, keeps the immutable snapshot alive.
  return 1;
}

extern "C" void __libunwind_mmix_forced_abort(void *exception, uintptr_t stop,
                                              void *argument) {
  check_quarantined_exception(exception);
  if (!stop || stop != managed_stop) {
    if (forced.exception && forced.exception == exception)
      __builtin_trap();
    return;
  }
  if (phase() != Landing || !matches(exception, stop, argument) || query)
    __builtin_trap();
  publish(Changing);
  release(forced.held, false);
  clear_forced();
}
extern "C" void *__libunwind_mmix_cursor_query_enter(void *cursor) {
  check_quarantined_cursor(cursor);
  void *previous = query;
  query = cursor;
  return previous;
}
extern "C" void __libunwind_mmix_cursor_query_leave(void *previous) {
  query = previous;
}
#if defined(_LIBUNWIND_MMIX_STARTUP_IMAGES) ||                                  \
    !defined(_LIBUNWIND_MMIX_CURSOR_LIFETIME)
bool mmixFindCursorImage(uintptr_t pc, MMIXUnwindImage &view, bool &owned) {
  check_quarantined_cursor(query);
  auto *owner = query ? find(query) : nullptr;
  owned = owner != nullptr;
  if (!owner)
    return __libunwind_mmix_find_startup_image(pc, &view);
  if (owner->root_image)
    return __libunwind_mmix_find_root_image(pc, &view);
  if (owner->token) {
    if (__libunwind_mmix_lookup_image(owner->token, pc, &view))
      return true;
  } else if (__libunwind_mmix_acquire_image(pc, &view)) {
    owner->token = view.token;
    return true;
  }
  if (owner->fallback_token)
    return __libunwind_mmix_lookup_image(owner->fallback_token, pc, &view);
  // A destructor can throw and catch an ordinary exception after a peer has
  // closed its DSO. Its fresh cursor needs both newly loaded code and the old
  // continuation's view. Retain an independent snapshot owner only on this miss.
  uintptr_t inherited = 0;
  unsigned current = phase();
  if (current == Landing)
    inherited = forced.held.token;
  else if ((current == Walking || current == Stopped) &&
           forced.cursor != owner->cursor) {
    if (auto *parent = find(forced.cursor))
      inherited = parent->token;
  }
  if (!inherited || !__libunwind_mmix_lookup_image(inherited, pc, &view))
    return false;
  MMIXUnwindImage copy{};
  if (!__libunwind_mmix_retain_image(inherited, &copy))
    return false;
  owner->fallback_token = copy.token;
  view.token = copy.token;
  return true;
}
#endif
