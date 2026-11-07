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

static unsigned failures;
static unsigned stops;

#if FAILURE_STAGE == 0
extern "C" int __wrap___unw_getcontext(unw_context_t *) {
  ++failures;
  return UNW_EUNSPEC;
}
#elif FAILURE_STAGE == 1
extern "C" int __wrap___unw_init_local(unw_cursor_t *, unw_context_t *) {
  ++failures;
  return UNW_EINVAL;
}
#elif FAILURE_STAGE == 2
extern "C" int __wrap___unw_step(unw_cursor_t *) {
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

int main() {
  _Unwind_Exception exception = {};
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
