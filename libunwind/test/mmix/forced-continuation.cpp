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
#include <sys/wait.h>
#include <unistd.h>

// Include the production helper to inject the otherwise instruction-sized
// Changing phase deterministically; no runtime test hook is exported.
#include "mmix/CursorLifetime.cpp"

static constexpr uintptr_t Stop = 123;
static unsigned acquires, drops, references;
static bool published = true;

#ifdef _LIBUNWIND_MMIX_STARTUP_IMAGES
extern "C" bool __libunwind_mmix_find_startup_image(uintptr_t,
                                                  MMIXUnwindImage *) {
  assert(false);
  return false;
}
extern "C" bool __libunwind_mmix_acquire_image(uintptr_t,
                                             MMIXUnwindImage *view) {
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
  assert(token == 42 && references);
  *view = {};
  view->token = token;
  return true;
}
extern "C" bool __libunwind_mmix_drop_image(uintptr_t token) {
  assert(token == 42 && references);
  --references;
  ++drops;
  return true;
}
extern "C" bool __libunwind_mmix_retain_image(uintptr_t token,
                                            MMIXUnwindImage *view) {
  assert(token == 42 && references);
  ++references;
  *view = {};
  view->token = token;
  return true;
}
extern "C" bool __libunwind_mmix_pin_image(uintptr_t) {
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
}
