//===-- Managed forced continuation ownership tests -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Production lifecycle hooks, with a counted image-service substitute. Real
// landing pads and loader reclamation require the separate target observer.
#include "CursorLifetime.h"
#include <cassert>
#include <pthread.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

// Include the production helper to inject the otherwise instruction-sized
// Changing phase deterministically; no runtime test hook is exported.
#include "mmix/CursorLifetime.cpp"

static constexpr uintptr_t Stop = 123;
static unsigned acquires, drops, references, image_calls;
static bool published = true;

#ifdef _LIBUNWIND_MMIX_STARTUP_IMAGES
extern "C" bool __libunwind_mmix_find_startup_image(uintptr_t,
                                                  MMIXUnwindImage *) {
  ++image_calls;
  assert(false);
  return false;
}
extern "C" bool __libunwind_mmix_acquire_image(uintptr_t,
                                             MMIXUnwindImage *view) {
  ++image_calls;
  if (!published)
    return false;
  ++acquires;
  ++references;
  *view = {};
  view->token = 42;
  return true;
}
extern "C" bool __libunwind_mmix_lookup_image(uintptr_t token, uintptr_t,
                                            MMIXUnwindImage *view) {
  ++image_calls;
  assert(token == 42 && references);
  *view = {};
  view->token = token;
  return true;
}
extern "C" bool __libunwind_mmix_drop_image(uintptr_t token) {
  ++image_calls;
  assert(token == 42 && references);
  --references;
  ++drops;
  return true;
}
extern "C" bool __libunwind_mmix_retain_image(uintptr_t token,
                                            MMIXUnwindImage *view) {
  ++image_calls;
  assert(token == 42 && references);
  ++references;
  *view = {};
  view->token = token;
  return true;
}
extern "C" bool __libunwind_mmix_pin_image(uintptr_t) {
  ++image_calls;
  assert(false && "managed cleanup must never pin an image permanently");
  return false;
}
#endif

static void lookup(void *cursor) {
#ifdef _LIBUNWIND_MMIX_STARTUP_IMAGES
  MMIXCursorQuery scope(cursor);
  MMIXUnwindImage view;
  bool owned = false;
  assert(mmixFindCursorImage(1000, view, owned) && owned && view.token == 42);
#else
  (void)cursor;
#endif
}

static MMIXCursorCheckpoint checkpoint() {
  MMIXCursorCheckpoint result;
  __libunwind_mmix_cursor_checkpoint(&result);
  assert(__libunwind_mmix_cursor_at_checkpoint(&result));
  return result;
}

static bool same_owner(const MMIXCursorLifetime &a,
                       const MMIXCursorLifetime &b) {
  return a.cursor == b.cursor && a.token == b.token &&
         a.previous == b.previous && a.fallback_token == b.fallback_token;
}

struct LifetimeSnapshot {
  ForcedContinuation continuation = forced;
  MMIXCursorLifetime *head = owners;
  MMIXCursorLifetime head_value = head ? *head : MMIXCursorLifetime{};
  MMIXCursorLifetime *active = forced.cursor ? find(forced.cursor) : nullptr;
  MMIXCursorLifetime active_value = active ? *active : MMIXCursorLifetime{};
  void *active_query = query;
  uintptr_t stop = managed_stop;
  unsigned calls = image_calls, acquired = acquires, dropped = drops;
  unsigned retained = references;

  bool unchanged(unsigned expected_phase) const {
    return phase() == expected_phase &&
           forced.exception == continuation.exception &&
           forced.stop == continuation.stop &&
           forced.argument == continuation.argument &&
           forced.cursor == continuation.cursor &&
           forced.base == continuation.base &&
           same_owner(forced.held, continuation.held) && owners == head &&
           (!head || same_owner(*head, head_value)) &&
           (!active || same_owner(*active, active_value)) &&
           query == active_query && managed_stop == stop &&
           image_calls == calls && acquires == acquired && drops == dropped &&
           references == retained;
  }
};

static void reject_quarantine(const MMIXCursorCheckpoint *base, void *exception,
                              uintptr_t stop, void *argument,
                              const MMIXForcedCheckpoint *saved) {
  LifetimeSnapshot before;
  assert(!__libunwind_mmix_forced_quarantine(base, exception, stop, argument,
                                            saved));
  assert(before.unchanged(before.continuation.phase));
}

static void *foreign_observation(void *argument) {
  const auto &saved = *static_cast<const MMIXForcedCheckpoint *>(argument);
  auto base = checkpoint();
  __libunwind_mmix_set_managed_stop(Stop);
  assert(!__libunwind_mmix_forced_validate(
      &base, saved.exception, saved.stop, saved.argument, &saved));
  reject_quarantine(&base, saved.exception, saved.stop, saved.argument, &saved);
  return nullptr;
}

static void observation_tests(const MMIXCursorCheckpoint &base) {
  int exception, argument, other;
  MMIXForcedCheckpoint saved{}, stopped{};
  unsigned calls = image_calls;
  auto capture = [&](const MMIXCursorCheckpoint &from, void *ex, uintptr_t stop,
                     void *arg, MMIXForcedCheckpoint &out) {
    return __libunwind_mmix_forced_capture(&from, ex, stop, arg, &out);
  };
  auto validate = [&](const MMIXForcedCheckpoint &value) {
    return __libunwind_mmix_forced_validate(&base, &exception, Stop, &argument,
                                           &value);
  };
  auto rejected = [&] {
    MMIXForcedCheckpoint output = saved;
    assert(!capture(base, &exception, Stop, &argument, output));
    // Failure must not even clear the caller's borrowed output.
    assert(output.thread == saved.thread && output.exception == saved.exception &&
           output.stop == saved.stop && output.argument == saved.argument &&
           output.base == saved.base && output.owner == saved.owner &&
           output.cursor == saved.cursor && output.token == saved.token &&
           output.fallback_token == saved.fallback_token &&
           output.held_token == saved.held_token &&
           output.held_fallback_token == saved.held_fallback_token &&
           output.phase == saved.phase);
    assert(!validate(saved));
    reject_quarantine(&base, &exception, Stop, &argument, &saved);
  };
  auto accepted = [&](unsigned expected) {
    auto before = checkpoint();
    assert(capture(base, &exception, Stop, &argument, saved));
    assert(saved.phase == expected && saved.thread == base.thread &&
           saved.exception == &exception && saved.stop == Stop &&
           saved.argument == &argument && !saved.base);
    assert(validate(saved));
    assert(__libunwind_mmix_cursor_at_checkpoint(&before));
    assert(image_calls == calls);
    // Every field is an identity, including slots unused in this phase.
    for (unsigned field = 0; field != 12; ++field) {
      auto wrong = saved;
      switch (field) {
      case 0:
        ++wrong.thread;
        break;
      case 1:
        wrong.exception = &other;
        break;
      case 2:
        ++wrong.stop;
        break;
      case 3:
        wrong.argument = &other;
        break;
      case 4:
        wrong.base = reinterpret_cast<MMIXCursorLifetime *>(1);
        break;
      case 5:
        wrong.owner = reinterpret_cast<MMIXCursorLifetime *>(1);
        break;
      case 6:
        wrong.cursor = &other;
        break;
      case 7:
        ++wrong.token;
        break;
      case 8:
        ++wrong.fallback_token;
        break;
      case 9:
        ++wrong.held_token;
        break;
      case 10:
        ++wrong.held_fallback_token;
        break;
      case 11:
        ++wrong.phase;
        break;
      }
      assert(!validate(wrong));
      reject_quarantine(&base, &exception, Stop, &argument, &wrong);
      assert(validate(saved));
    }
    MMIXForcedCheckpoint output{};
    assert(!capture(base, nullptr, Stop, &argument, output));
    assert(!capture(base, &other, Stop, &argument, output));
    assert(!capture(base, &exception, 0, &argument, output));
    assert(!capture(base, &exception, Stop + 1, &argument, output));
    assert(!capture(base, &exception, Stop, &other, output));
    assert(!__libunwind_mmix_forced_validate(&base, &other, Stop, &argument,
                                            &saved));
    assert(!__libunwind_mmix_forced_validate(&base, &exception, Stop + 1,
                                            &argument, &saved));
    assert(!__libunwind_mmix_forced_validate(&base, &exception, Stop, &other,
                                            &saved));
    assert(!__libunwind_mmix_forced_capture(nullptr, &exception, Stop, &argument,
                                           &output));
    assert(!__libunwind_mmix_forced_capture(&base, &exception, Stop, &argument,
                                           nullptr));
    assert(!__libunwind_mmix_forced_validate(&base, &exception, Stop, &argument,
                                            nullptr));
    reject_quarantine(nullptr, &exception, Stop, &argument, &saved);
    reject_quarantine(&base, &exception, Stop, &argument, nullptr);
    reject_quarantine(&base, nullptr, Stop, &argument, &saved);
    reject_quarantine(&base, &other, Stop, &argument, &saved);
    reject_quarantine(&base, &exception, 0, &argument, &saved);
    reject_quarantine(&base, &exception, Stop + 1, &argument, &saved);
    reject_quarantine(&base, &exception, Stop, &other, &saved);
    for (unsigned field = 0; field != 5; ++field) {
      auto wrong = base;
      switch (field) {
      case 0:
        ++wrong.thread;
        break;
      case 1:
        wrong.owners = reinterpret_cast<MMIXCursorLifetime *>(1);
        break;
      case 2:
        wrong.query = &other;
        break;
      case 3:
        wrong.forced_exception = &exception;
        break;
      case 4:
        wrong.forced_phase = Stopped;
        break;
      }
      assert(!capture(wrong, &exception, Stop, &argument, output));
      assert(!__libunwind_mmix_forced_validate(&wrong, &exception, Stop, &argument,
                                              &saved));
      reject_quarantine(&wrong, &exception, Stop, &argument, &saved);
    }
    {
      int nested;
      _LIBUNWIND_CURSOR_SCOPE(nested);
      rejected();
    }
    {
      MMIXCursorQuery scope(&other); // Also reject an untracked/public query.
      rejected();
    }
    forced.base = reinterpret_cast<MMIXCursorLifetime *>(1);
    rejected();
    forced.base = nullptr;
    assert(validate(saved));
    pthread_t thread;
    assert(!pthread_create(&thread, nullptr, foreign_observation, &saved));
    assert(!pthread_join(thread, nullptr));
    assert(validate(saved));
    assert(image_calls == calls);
  };

  rejected(); // Empty is not a forced cleanup.
  {
    int cursor;
    _LIBUNWIND_CURSOR_SCOPE_WITH_STOP(cursor, Stop);
    assert(_LIBUNWIND_FORCED_ENTER(&cursor, &exception, Stop, &argument));
    rejected(); // Walking is not a cleanup boundary.
    publish(Changing);
    rejected();
    publish(Walking);
    assert(_LIBUNWIND_FORCED_STOP(&cursor, &exception, Stop, &argument, 1));
#ifdef _LIBUNWIND_MMIX_STARTUP_IMAGES
    // Deliberately opaque, non-provider tokens: observation must not resolve,
    // retain or drop either slot. Static mode exercises the same shape at zero.
    cursor_lifetime.token = 101;
    cursor_lifetime.fallback_token = 202;
#endif
    accepted(MMIXForcedStopped);
    assert(saved.owner == &cursor_lifetime && saved.cursor == &cursor &&
           saved.token == cursor_lifetime.token &&
           saved.fallback_token == cursor_lifetime.fallback_token &&
           !saved.held_token && !saved.held_fallback_token);
#ifdef _LIBUNWIND_MMIX_STARTUP_IMAGES
    assert(saved.token == 101 && saved.fallback_token == 202);
#else
    assert(!saved.token && !saved.fallback_token);
#endif
    stopped = saved;
    ++cursor_lifetime.token;
    assert(!validate(saved));
    reject_quarantine(&base, &exception, Stop, &argument, &saved);
    --cursor_lifetime.token;
    ++cursor_lifetime.fallback_token;
    assert(!validate(saved));
    reject_quarantine(&base, &exception, Stop, &argument, &saved);
    --cursor_lifetime.fallback_token;
    {
      MMIXCursorQuery scope(&cursor);
      rejected();
    }
    forced.cursor = &other;
    rejected();
    forced.cursor = &cursor;
    forced.held.token = 1;
    rejected();
    forced.held.token = 0;
    forced.held.fallback_token = 1;
    rejected();
    forced.held.fallback_token = 0;
    assert(validate(saved));
    assert(_LIBUNWIND_FORCED_STOP(&cursor, &exception, Stop, &argument, 0));
    rejected();
    assert(_LIBUNWIND_FORCED_LANDING(&cursor, &exception, Stop, &argument));
    rejected(); // Published Landing still has the old stack sidecar here.
    _LIBUNWIND_CURSOR_FINISH(&cursor);
  }
  assert(!validate(stopped));
  reject_quarantine(&base, &exception, Stop, &argument, &stopped);
  accepted(MMIXForcedLanding);
  assert(!saved.owner && !saved.cursor && !saved.token && !saved.fallback_token &&
         saved.held_token == stopped.token &&
         saved.held_fallback_token == stopped.fallback_token);
  ++forced.held.token;
  assert(!validate(saved));
  reject_quarantine(&base, &exception, Stop, &argument, &saved);
  --forced.held.token;
  ++forced.held.fallback_token;
  assert(!validate(saved));
  reject_quarantine(&base, &exception, Stop, &argument, &saved);
  --forced.held.fallback_token;
  assert(validate(saved));
  // Remove test-only opaque values before real lifecycle disposal.
  forced.held.token = forced.held.fallback_token = 0;
  _LIBUNWIND_FORCED_ABORT(&exception, Stop, &argument);
  rejected(); // A finished continuation cannot validate its old identity.
  assert(__libunwind_mmix_cursor_at_checkpoint(&base));

  // Even a matching, actually captured nonempty base is outside this API.
  {
    int outer;
    _LIBUNWIND_CURSOR_SCOPE(outer);
    auto occupied = checkpoint();
    {
      int cursor;
      _LIBUNWIND_CURSOR_SCOPE_WITH_STOP(cursor, Stop);
      assert(_LIBUNWIND_FORCED_ENTER(&cursor, &exception, Stop, &argument));
      assert(_LIBUNWIND_FORCED_STOP(&cursor, &exception, Stop, &argument, 1));
      rejected();
      assert(!capture(occupied, &exception, Stop, &argument, saved));
      reject_quarantine(&occupied, &exception, Stop, &argument, &saved);
      assert(_LIBUNWIND_FORCED_STOP(&cursor, &exception, Stop, &argument, 0));
      assert(_LIBUNWIND_FORCED_LANDING(&cursor, &exception, Stop, &argument));
      _LIBUNWIND_CURSOR_FINISH(&cursor);
    }
    rejected();
    assert(!capture(occupied, &exception, Stop, &argument, saved));
    reject_quarantine(&occupied, &exception, Stop, &argument, &saved);
    _LIBUNWIND_FORCED_ABORT(&exception, Stop, &argument);
  }
  assert(__libunwind_mmix_cursor_at_checkpoint(&base));
  assert(image_calls == calls);
}

static const LifetimeSnapshot *trap_snapshot;

static void quarantine_trap(int signal) {
  // Check at the fault, not just before the operation: even a trap after one
  // metadata call, query change, lease transfer or owner release is too late.
  if (!trap_snapshot || !trap_snapshot->unchanged(Quarantined))
    _exit(90);
  // SA_RESETHAND restores the default disposition. Deliver the original trap
  // signal after this handler so the parent still requires signal termination.
  if (kill(getpid(), signal))
    _exit(91);
}

enum class QuarantineAction {
  Preserve,
  Resume,
  ResumeUnmanaged,
  ResumeArgument,
  Enter,
  EnterUnmanaged,
  StopReturn,
  StopUnmanaged,
  Landing,
  Abort,
  AbortUnmanaged,
  Release,
  ReleaseEscape,
  // These operations require the still-live Stopped cursor.
  End,
  Finish,
  FinishAt,
  FinishAtInvalid,
  Reset,
  Escape,
  CopyFrom,
  CopyTo,
  Query,
  Begin,
  EnterOther,
  StopOther,
  LandingOther,
  Count
};

static void quarantine_children(const MMIXCursorCheckpoint &base,
                                const MMIXForcedCheckpoint &saved) {
  LifetimeSnapshot parent;
  auto suspended = checkpoint();
  for (unsigned index = 0; index != unsigned(QuarantineAction::Count); ++index) {
    auto action = static_cast<QuarantineAction>(index);
    if (saved.phase == MMIXForcedLanding && action >= QuarantineAction::End)
      continue;
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
      struct rlimit no_core = {0, 0};
      assert(!setrlimit(RLIMIT_CORE, &no_core));
      assert(__libunwind_mmix_forced_quarantine(
          &base, saved.exception, saved.stop, saved.argument, &saved));
      assert(parent.unchanged(Quarantined));
      assert(*static_cast<int *>(saved.exception) == 11 &&
             *static_cast<int *>(saved.argument) == 12 &&
             (!saved.cursor || *static_cast<int *>(saved.cursor) == 13));
      assert(!__libunwind_mmix_cursor_at_checkpoint(&suspended));
      assert(!__libunwind_mmix_cursor_at_checkpoint(&base));
      MMIXForcedCheckpoint output = saved;
      assert(!__libunwind_mmix_forced_capture(
          &base, saved.exception, saved.stop, saved.argument, &output));
      assert(!__libunwind_mmix_forced_validate(
          &base, saved.exception, saved.stop, saved.argument, &saved));
      reject_quarantine(&base, saved.exception, saved.stop, saved.argument,
                        &saved);
      auto quarantined = checkpoint();

      // Ordinary independent cursors and unmanaged exceptions are not the
      // quarantined continuation. Their lifecycle hooks remain usable.
      int other = 17, scratch = 23;
      assert(_LIBUNWIND_FORCED_RESUME(&other, 0, nullptr));
      assert(_LIBUNWIND_FORCED_ENTER(&scratch, &other, Stop + 1, nullptr));
      assert(_LIBUNWIND_FORCED_STOP(&scratch, &other, Stop + 1, nullptr, 0));
      assert(_LIBUNWIND_FORCED_LANDING(&scratch, &other, Stop + 1, nullptr));
      _LIBUNWIND_FORCED_ABORT(&other, Stop + 1, nullptr);
      {
        int ordinary;
        _LIBUNWIND_CURSOR_SCOPE(ordinary);
        MMIXCursorQuery scope(&ordinary);
      }
      assert(__libunwind_mmix_cursor_at_checkpoint(&quarantined));
      assert(parent.unchanged(Quarantined));
      if (action == QuarantineAction::Preserve)
        _exit(0);

      MMIXCursorLifetime scratch_owner{};
      if (action == QuarantineAction::Enter ||
          action == QuarantineAction::EnterUnmanaged ||
          action == QuarantineAction::CopyFrom ||
          action == QuarantineAction::CopyTo)
        __libunwind_mmix_cursor_begin(&scratch_owner, &scratch);
      LifetimeSnapshot before_trap;
      trap_snapshot = &before_trap;
      struct sigaction handler{};
      handler.sa_handler = quarantine_trap;
      handler.sa_flags = SA_RESETHAND;
      assert(!sigemptyset(&handler.sa_mask));
      assert(!sigaction(SIGILL, &handler, nullptr));
      assert(!sigaction(SIGTRAP, &handler, nullptr));
      switch (action) {
      case QuarantineAction::Resume:
        _LIBUNWIND_FORCED_RESUME(saved.exception, Stop, saved.argument);
        break;
      case QuarantineAction::ResumeUnmanaged:
        _LIBUNWIND_FORCED_RESUME(saved.exception, 0, saved.argument);
        break;
      case QuarantineAction::ResumeArgument:
        _LIBUNWIND_FORCED_RESUME(saved.exception, Stop, &other);
        break;
      case QuarantineAction::Enter:
        _LIBUNWIND_FORCED_ENTER(&scratch, saved.exception, Stop, saved.argument);
        break;
      case QuarantineAction::EnterUnmanaged:
        _LIBUNWIND_FORCED_ENTER(&scratch, saved.exception, Stop + 1, &other);
        break;
      case QuarantineAction::StopReturn:
        _LIBUNWIND_FORCED_STOP(saved.cursor, saved.exception, Stop,
                               saved.argument, 0);
        break;
      case QuarantineAction::StopUnmanaged:
        _LIBUNWIND_FORCED_STOP(saved.cursor, saved.exception, Stop + 1, &other, 1);
        break;
      case QuarantineAction::Landing:
        _LIBUNWIND_FORCED_LANDING(saved.cursor, saved.exception, Stop,
                                  saved.argument);
        break;
      case QuarantineAction::Abort:
        _LIBUNWIND_FORCED_ABORT(saved.exception, Stop, saved.argument);
        break;
      case QuarantineAction::AbortUnmanaged:
        _LIBUNWIND_FORCED_ABORT(saved.exception, Stop + 1, &other);
        break;
      case QuarantineAction::Release:
      case QuarantineAction::ReleaseEscape:
        release(saved.owner ? *saved.owner : forced.held,
                action == QuarantineAction::ReleaseEscape);
        break;
      case QuarantineAction::End:
        __libunwind_mmix_cursor_end(saved.owner);
        break;
      case QuarantineAction::Finish:
        __libunwind_mmix_cursor_finish(saved.cursor);
        break;
      case QuarantineAction::FinishAt:
        __libunwind_mmix_cursor_finish_at(saved.cursor, &base);
        break;
      case QuarantineAction::FinishAtInvalid:
        __libunwind_mmix_cursor_finish_at(saved.cursor, nullptr);
        break;
      case QuarantineAction::Reset:
        __libunwind_mmix_cursor_reset(saved.cursor);
        break;
      case QuarantineAction::Escape:
        __libunwind_mmix_cursor_escape(saved.cursor);
        break;
      case QuarantineAction::CopyFrom:
        __libunwind_mmix_cursor_copy(&scratch, saved.cursor);
        break;
      case QuarantineAction::CopyTo:
        __libunwind_mmix_cursor_copy(saved.cursor, &scratch);
        break;
      case QuarantineAction::Query:
        __libunwind_mmix_cursor_query_enter(saved.cursor);
        break;
      case QuarantineAction::Begin:
        __libunwind_mmix_cursor_begin(&scratch_owner, saved.cursor);
        break;
      case QuarantineAction::EnterOther:
        _LIBUNWIND_FORCED_ENTER(saved.cursor, &other, Stop + 1, nullptr);
        break;
      case QuarantineAction::StopOther:
        _LIBUNWIND_FORCED_STOP(saved.cursor, &other, Stop + 1, nullptr, 0);
        break;
      case QuarantineAction::LandingOther:
        _LIBUNWIND_FORCED_LANDING(saved.cursor, &other, Stop + 1, nullptr);
        break;
      default:
        _exit(92);
      }
      _exit(93); // Returning a generic unwind failure is not a raw trap.
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    if (action == QuarantineAction::Preserve)
      assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    else
      assert(WIFSIGNALED(status) &&
             (WTERMSIG(status) == SIGILL || WTERMSIG(status) == SIGTRAP));
    assert(parent.unchanged(parent.continuation.phase));
    assert(__libunwind_mmix_cursor_at_checkpoint(&suspended));
    assert(__libunwind_mmix_forced_validate(
        &base, saved.exception, saved.stop, saved.argument, &saved));
  }
}

static void quarantine_tests(const MMIXCursorCheckpoint &base) {
  unsigned acquired = acquires, dropped = drops, retained = references;
  int exception = 11, argument = 12, cursor = 13;
  MMIXForcedCheckpoint saved{};
  {
    _LIBUNWIND_CURSOR_SCOPE_WITH_STOP(cursor, Stop);
    assert(_LIBUNWIND_FORCED_ENTER(&cursor, &exception, Stop, &argument));
    lookup(&cursor);
#ifdef _LIBUNWIND_MMIX_STARTUP_IMAGES
    // Keep actual counted leases in both slots across the fork. Each child's
    // quarantine must preserve them, while the parent can still retire them.
    MMIXUnwindImage fallback{};
    assert(__libunwind_mmix_retain_image(cursor_lifetime.token, &fallback));
    cursor_lifetime.fallback_token = fallback.token;
    assert(references == retained + 2);
#endif
    assert(_LIBUNWIND_FORCED_STOP(&cursor, &exception, Stop, &argument, 1));
    assert(__libunwind_mmix_forced_capture(&base, &exception, Stop, &argument,
                                           &saved));
    quarantine_children(base, saved);
    assert(_LIBUNWIND_FORCED_STOP(&cursor, &exception, Stop, &argument, 0));
    assert(_LIBUNWIND_FORCED_LANDING(&cursor, &exception, Stop, &argument));
    _LIBUNWIND_CURSOR_FINISH(&cursor);
  }
  assert(__libunwind_mmix_forced_capture(&base, &exception, Stop, &argument,
                                         &saved));
  quarantine_children(base, saved);
  _LIBUNWIND_FORCED_ABORT(&exception, Stop, &argument);
  assert(exception == 11 && argument == 12 && cursor == 13);
  assert(__libunwind_mmix_cursor_at_checkpoint(&base));
  assert(references == retained);
#ifdef _LIBUNWIND_MMIX_STARTUP_IMAGES
  assert(acquires == acquired + 1 && drops == dropped + 2);
#else
  assert(acquires == acquired && drops == dropped);
#endif
}

static void *foreign(void *argument) {
  auto *saved = static_cast<MMIXCursorCheckpoint *>(argument);
  auto empty = checkpoint();
  assert(!empty.forced_exception && !empty.forced_phase);
  assert(!__libunwind_mmix_cursor_at_checkpoint(saved));
  int cursor;
  _LIBUNWIND_CURSOR_SCOPE_WITH_STOP(cursor, Stop);
  assert(!cursor_lifetime.cursor); // Registration is thread-local.
  return nullptr;
}

static void resume_and_finish(void *exception, void *argument,
                              const MMIXCursorCheckpoint &empty) {
  for (unsigned round = 0; round != 3; ++round) {
    int cursor, other;
    _LIBUNWIND_CURSOR_SCOPE_WITH_STOP(cursor, Stop);
    auto before = checkpoint();
    assert(!before.forced_exception || before.forced_exception == exception);
    assert(!_LIBUNWIND_FORCED_RESUME(exception, 0, argument));
    assert(!_LIBUNWIND_FORCED_RESUME(exception, Stop + 1, argument));
    assert(!_LIBUNWIND_FORCED_RESUME(exception, Stop, &other));
    assert(!_LIBUNWIND_FORCED_RESUME(&other, Stop, argument));
    assert(_LIBUNWIND_FORCED_RESUME(exception, Stop, argument));
    assert(!_LIBUNWIND_FORCED_ENTER(&cursor, &other, Stop, argument));
    assert(!_LIBUNWIND_FORCED_ENTER(&cursor, exception, Stop, &other));
    assert(!_LIBUNWIND_FORCED_ENTER(&cursor, exception, Stop + 1, argument));
    assert(!_LIBUNWIND_FORCED_ENTER(&cursor, exception, 0, argument));
    assert(__libunwind_mmix_cursor_at_checkpoint(&before));
    assert(_LIBUNWIND_FORCED_ENTER(&cursor, exception, Stop, argument));
    assert(!_LIBUNWIND_FORCED_RESUME(exception, Stop, argument));
    lookup(&cursor); // Must use the retained view after logical removal.
    assert(_LIBUNWIND_FORCED_STOP(&cursor, exception, Stop, argument, 1));
    assert(!_LIBUNWIND_FORCED_LANDING(&cursor, exception, Stop, argument));
    if (round == 2) {
      // The libc terminal root and signal-segment transfer use this disposal.
      assert(__libunwind_mmix_cursor_finish_at(&cursor, &empty));
      assert(__libunwind_mmix_cursor_at_checkpoint(&empty));
      return;
    }
    assert(_LIBUNWIND_FORCED_STOP(&cursor, exception, Stop, argument, 0));
    assert(_LIBUNWIND_FORCED_LANDING(&cursor, exception, Stop, argument));
    _LIBUNWIND_CURSOR_FINISH(&cursor);
    assert(!__libunwind_mmix_cursor_at_checkpoint(&empty));
  }
}

int main() {
  auto empty = checkpoint();
  __libunwind_mmix_set_managed_stop(Stop);
  observation_tests(empty);
  int exception, argument, other;
  {
    int raw;
    _LIBUNWIND_CURSOR_SCOPE_WITH_STOP(raw, Stop + 1);
    assert(_LIBUNWIND_FORCED_ENTER(&raw, &exception, Stop + 1, &argument));
    assert(_LIBUNWIND_FORCED_STOP(&raw, &exception, Stop + 1, &argument, 1));
    assert(_LIBUNWIND_FORCED_LANDING(&raw, &exception, Stop + 1, &argument));
    _LIBUNWIND_FORCED_ABORT(&exception, Stop + 1, &argument);
    assert(__libunwind_mmix_cursor_at_checkpoint(&empty));
  }
  {
    int cursor;
    _LIBUNWIND_CURSOR_SCOPE_WITH_STOP(cursor, Stop);
    assert(_LIBUNWIND_FORCED_ENTER(&cursor, &exception, Stop, &argument));
    lookup(&cursor);
    assert(_LIBUNWIND_FORCED_STOP(&cursor, &exception, Stop, &argument, 1));
    auto stopped = checkpoint();
    assert(!_LIBUNWIND_FORCED_STOP(&cursor, &other, Stop, &argument, 0));
    assert(!_LIBUNWIND_FORCED_STOP(&cursor, &exception, Stop, &other, 0));
    assert(__libunwind_mmix_cursor_at_checkpoint(&stopped));
    assert(_LIBUNWIND_FORCED_STOP(&cursor, &exception, Stop, &argument, 0));
    // Normal signal return must accept the exact suspended transition, while
    // a second forced unwind cannot use that witness as admission.
    publish(Changing);
    auto changing = checkpoint();
    {
      int handler_cursor;
      _LIBUNWIND_CURSOR_SCOPE(handler_cursor);
      assert(!_LIBUNWIND_FORCED_ENTER(&handler_cursor, &other, Stop, &argument));
      assert(!__libunwind_mmix_cursor_at_checkpoint(&changing));
    }
    assert(__libunwind_mmix_cursor_at_checkpoint(&changing));
    publish(Walking);
    {
      MMIXCursorQuery scope(&cursor);
      assert(!_LIBUNWIND_FORCED_LANDING(&cursor, &exception, Stop, &argument));
    }
    {
      int nested;
      _LIBUNWIND_CURSOR_SCOPE(nested);
      assert(!_LIBUNWIND_FORCED_LANDING(&cursor, &exception, Stop, &argument));
      assert(!_LIBUNWIND_FORCED_ENTER(&nested, &exception, Stop, &argument));
      assert(!_LIBUNWIND_CURSOR_COPY(&cursor, &nested));
    }
    assert(!_LIBUNWIND_FORCED_LANDING(&other, &exception, Stop, &argument));
    assert(_LIBUNWIND_FORCED_LANDING(&cursor, &exception, Stop, &argument));
    assert(!cursor_lifetime.token);
    _LIBUNWIND_CURSOR_FINISH(&cursor);
  }
  auto landing = checkpoint();
  assert(landing.forced_exception == &exception && !landing.owners);
  assert(!__libunwind_mmix_cursor_at_checkpoint(&empty));
  assert(drops == 0);
  published = false;
  // A regular nested exception must not consume the suspended forced owner.
  try {
    int temporary;
    _LIBUNWIND_CURSOR_SCOPE(temporary);
    lookup(&temporary); // Exercise fallback discovery, not only host EH.
    throw 7;
  } catch (int value) {
    assert(value == 7);
  }
  assert(__libunwind_mmix_cursor_at_checkpoint(&landing));
  pthread_t thread;
  assert(!pthread_create(&thread, nullptr, foreign, &landing));
  assert(!pthread_join(thread, nullptr));
  assert(__libunwind_mmix_cursor_at_checkpoint(&landing));

  // Copy-on-write duplicates both the owner and accounting, not a second
  // reference within one address space. This is a helper test, not MMIX fork.
  pid_t child = fork();
  assert(child >= 0);
  if (!child) {
    resume_and_finish(&exception, &argument, empty);
#ifdef _LIBUNWIND_MMIX_STARTUP_IMAGES
    assert(acquires == 1 && drops == 2 && references == 0);
#endif
    _exit(0);
  }
  int status;
  assert(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
         WEXITSTATUS(status) == 0);
#ifdef _LIBUNWIND_MMIX_STARTUP_IMAGES
  assert(drops == 1 && references == 1);
#endif
  resume_and_finish(&exception, &argument, empty);
#ifdef _LIBUNWIND_MMIX_STARTUP_IMAGES
  assert(acquires == 1 && drops == 2 && references == 0);
#endif

  // Initialization/stop failure returns through the original scope cleanup.
  published = true;
  {
    int cursor;
    _LIBUNWIND_CURSOR_SCOPE_WITH_STOP(cursor, Stop);
    assert(_LIBUNWIND_FORCED_ENTER(&cursor, &exception, Stop, &argument));
    lookup(&cursor);
  }
  assert(__libunwind_mmix_cursor_at_checkpoint(&empty));
  {
    int cursor;
    _LIBUNWIND_CURSOR_SCOPE_WITH_STOP(cursor, Stop);
    assert(_LIBUNWIND_FORCED_ENTER(&cursor, &exception, Stop, &argument));
    lookup(&cursor);
    assert(_LIBUNWIND_FORCED_LANDING(&cursor, &exception, Stop, &argument));
    _LIBUNWIND_FORCED_ABORT(&exception, Stop, &argument);
  }
  assert(__libunwind_mmix_cursor_at_checkpoint(&empty));
#ifdef _LIBUNWIND_MMIX_STARTUP_IMAGES
  assert(acquires == 3 && drops == 4 && references == 0);
#endif
  quarantine_tests(empty);
}
