// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Compose the real native unwind entry with the static lifetime protocol.
// Stop admitted probes at initialization, before native metadata traversal.
#include "mmix/CursorLifetime.h"
#include <assert.h>
#include <libunwind.h>
#include <unwind.h>

static unsigned initializations;
static unsigned stops;
static _Unwind_Exception *expected_exception;
static MMIXCursorLifetime *expected_parent;

extern "C" int __wrap___unw_init_local(unw_cursor_t *, unw_context_t *) {
  ++initializations;
  MMIXCursorCheckpoint current;
  __libunwind_mmix_cursor_checkpoint(&current);
  assert(current.owners && current.owners != expected_parent);
  assert(current.owners->previous == expected_parent);
  assert(current.forced_exception == expected_exception);
  assert(!current.query);
  return UNW_EINVAL;
}

static _Unwind_Reason_Code stop(int, _Unwind_Action, _Unwind_Exception_Class,
                               _Unwind_Exception *, _Unwind_Context *, void *) {
  ++stops;
  return _URC_FATAL_PHASE2_ERROR;
}

static void probe(bool admitted) {
  MMIXCursorCheckpoint before;
  __libunwind_mmix_cursor_checkpoint(&before);
  expected_parent = before.owners;
  for (unsigned retry = 0; retry != 2; ++retry) {
    _Unwind_Exception exception = {};
    expected_exception = &exception;
    int parameter;
    initializations = stops = 0;
    assert(_Unwind_ForcedUnwind(&exception, stop, &parameter) ==
           _URC_FATAL_PHASE2_ERROR);
    assert(initializations == (admitted ? 1u : 0u));
    assert(!stops);
    // Real context capture precedes private admission, even when rejected.
    assert(exception.private_1 == reinterpret_cast<uintptr_t>(stop));
    assert(exception.private_2 == reinterpret_cast<uintptr_t>(&parameter));
    assert(__libunwind_mmix_cursor_at_checkpoint(&before));
  }
}

int main() {
  const uintptr_t stop_address = reinterpret_cast<uintptr_t>(stop);
  __libunwind_mmix_set_managed_stop(stop_address);
  MMIXCursorCheckpoint empty;
  __libunwind_mmix_cursor_checkpoint(&empty);
  assert(!empty.owners && !empty.query && !empty.forced_exception);
  probe(true);

  unw_cursor_t parent_cursor;
  MMIXCursorLifetime parent = {};
  __libunwind_mmix_cursor_begin(&parent, &parent_cursor);
  {
    MMIXCursorQuery query(&parent_cursor);
    probe(false);
  }
  probe(true);

  // Seed a valid owned continuation using the production protocol. This is
  // not a claim that native metadata traversal or an outer landing occurred.
  _Unwind_Exception outer_exception = {};
  int outer_parameter;
  assert(__libunwind_mmix_forced_enter(&parent_cursor, &outer_exception,
                                      stop_address, &outer_parameter));
  probe(false);
  assert(__libunwind_mmix_forced_stop(&parent_cursor, &outer_exception,
                                     stop_address, &outer_parameter, 1));
  probe(false);
  assert(__libunwind_mmix_forced_stop(&parent_cursor, &outer_exception,
                                     stop_address, &outer_parameter, 0));
  __libunwind_mmix_cursor_end(&parent);
  assert(__libunwind_mmix_cursor_at_checkpoint(&empty));
  probe(true);
}
