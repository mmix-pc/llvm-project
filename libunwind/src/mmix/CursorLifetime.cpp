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

enum ForcedPhase : unsigned { Empty, Walking, Stopped, Landing, Changing };
struct ForcedContinuation {
  void *exception;
  uintptr_t stop;
  void *argument;
  void *cursor;
  MMIXCursorLifetime *base;
  MMIXCursorLifetime held;
  unsigned phase;
};
thread_local ForcedContinuation forced
    __attribute__((tls_model("initial-exec")));

unsigned phase() { return __atomic_load_n(&forced.phase, __ATOMIC_ACQUIRE); }
void publish(unsigned next) {
  __atomic_store_n(&forced.phase, next, __ATOMIC_RELEASE);
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
  if (!owner || !cursor || find(cursor))
    __builtin_trap();
  *owner = {cursor, 0, owners, 0};
  owners = owner;
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
extern "C" void __libunwind_mmix_cursor_escape(void *cursor) {
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
  if (cursor && forced.cursor == cursor)
    __builtin_trap();
  if (auto *owner = find(cursor))
    release(*owner, false);
}
extern "C" int __libunwind_mmix_cursor_copy(void *to, const void *from) {
  auto *source = find(from);
  auto *destination = find(to);
  if (!source || !destination || source == destination || forced.cursor == to)
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
  if (forced.exception && forced.exception == exception)
    return phase() == Landing && matches(exception, stop, argument) && !query;
  return !stop || stop != managed_stop;
}

extern "C" int __libunwind_mmix_forced_enter(void *cursor, void *exception,
                                             uintptr_t stop, void *argument) {
  if (!stop || stop != managed_stop)
    return !forced.exception || forced.exception != exception;
  auto *owner = find(cursor);
  unsigned current = phase();
  if (!exception || !owner || owners != owner || query || owner->token ||
      owner->fallback_token ||
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
  auto *owner = query ? find(query) : nullptr;
  owned = owner != nullptr;
  if (!owner)
    return __libunwind_mmix_find_startup_image(pc, &view);
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
  // continuation's view. Take an independent finite lease only on this miss.
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
