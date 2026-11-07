// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Linked against the real native provider with exactly one --wrap injection.
// Each failure occurs before callbacks or transfers; no synthetic saved
// context is allowed to escape a wrapper into real unwinding.
#include <assert.h>
#include <stdint.h>
#include <libunwind.h>
#include <unwind.h>

#ifdef TEST_MANAGED_CURSOR
#include "mmix/CursorLifetime.h"
static _Unwind_Exception *active_exception;
static MMIXCursorLifetime *parent_owner;
static void observe_owner() {
  MMIXCursorCheckpoint current;
  __libunwind_mmix_cursor_checkpoint(&current);
  assert(current.owners && current.owners != parent_owner);
  assert(current.owners->previous == parent_owner);
  assert(!current.query);
#if FAILURE_STAGE == 0
  assert(!current.forced_exception);
#else
  assert(current.forced_exception == active_exception);
#endif
}
#else
static void observe_owner() {}
#endif

static unsigned failures;
static unsigned stops;

#if FAILURE_STAGE == 0
extern "C" int __wrap___unw_getcontext(unw_context_t *) {
  observe_owner();
  ++failures;
  return UNW_EUNSPEC;
}
#elif FAILURE_STAGE == 1
extern "C" int __wrap___unw_init_local(unw_cursor_t *, unw_context_t *) {
  observe_owner();
  ++failures;
  return UNW_EINVAL;
}
#elif FAILURE_STAGE == 2
extern "C" int __wrap___unw_step(unw_cursor_t *) {
  observe_owner();
  ++failures;
  return UNW_EBADFRAME;
}
#else
#error Select one initialization failure stage
#endif

static _Unwind_Reason_Code stop(int, _Unwind_Action, _Unwind_Exception_Class,
                               _Unwind_Exception *, _Unwind_Context *, void *) {
  ++stops;
  return _URC_FATAL_PHASE2_ERROR;
}

static void attempt() {
  _Unwind_Exception exception = {};
#ifdef TEST_MANAGED_CURSOR
  active_exception = &exception;
#endif
  failures = stops = 0;
  exception.private_1 = 0x1234;
  exception.private_2 = 0x5678;
  int parameter;
  assert(_Unwind_ForcedUnwind(&exception, stop, &parameter) ==
         _URC_FATAL_PHASE2_ERROR);
  assert(failures == 1);
  assert(stops == 0);
#if FAILURE_STAGE == 0
  // A failed capture must not publish a forced-unwind continuation.
  assert(exception.private_1 == 0x1234);
  assert(exception.private_2 == 0x5678);
#else
  // Capture succeeded before the later initialization failure.
  assert(exception.private_1 == reinterpret_cast<uintptr_t>(stop));
  assert(exception.private_2 == reinterpret_cast<uintptr_t>(&parameter));
#endif
}

int main() {
#ifdef TEST_MANAGED_CURSOR
  __libunwind_mmix_set_managed_stop(reinterpret_cast<uintptr_t>(stop));
  MMIXCursorCheckpoint empty;
  __libunwind_mmix_cursor_checkpoint(&empty);
  assert(!empty.owners && !empty.query && !empty.forced_exception);
  for (unsigned nested = 0; nested != 2; ++nested) {
    unw_cursor_t parent_cursor;
    MMIXCursorLifetime parent = {};
    if (nested)
      __libunwind_mmix_cursor_begin(&parent, &parent_cursor);
    MMIXCursorCheckpoint before;
    __libunwind_mmix_cursor_checkpoint(&before);
    parent_owner = before.owners;
    for (unsigned retry = 0; retry != 2; ++retry) {
      attempt();
      assert(__libunwind_mmix_cursor_at_checkpoint(&before));
    }
    if (nested)
      __libunwind_mmix_cursor_end(&parent);
    assert(__libunwind_mmix_cursor_at_checkpoint(&empty));
  }
#else
  attempt();
#endif
}
