//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: target={{.*-aix.*}}, target={{.*-windows.*}}, no-exceptions
// Generic unregistered forced unwind must retain its ordinary cleanup/rethrow
// behavior when no target-private lifetime policy is selected.
#undef NDEBUG
#include <assert.h>
#include <stdlib.h>
#include <unwind.h>

static _Unwind_Exception exception;
static unsigned observations;

static void disposed(_Unwind_Reason_Code, _Unwind_Exception *) { abort(); }

static _Unwind_Reason_Code stop(int version, _Unwind_Action actions,
                               _Unwind_Exception_Class,
                               _Unwind_Exception *object,
                               _Unwind_Context *, void *argument) {
  assert(version == 1 && object == &exception && argument == &observations);
  assert((actions & (_UA_FORCE_UNWIND | _UA_CLEANUP_PHASE)) ==
         (_UA_FORCE_UNWIND | _UA_CLEANUP_PHASE));
  assert(!(actions & _UA_END_OF_STACK));
  if (observations == 7)
    _Exit(0);
  return _URC_NO_REASON;
}

__attribute__((noinline)) static void nested_throw() { throw 17; }

struct Guard {
  unsigned bit;
  __attribute__((noinline)) ~Guard() {
    assert(!(observations & bit));
    if (bit == 1) {
      try {
        nested_throw();
      } catch (int value) {
        assert(value == 17);
      }
    }
    observations |= bit;
  }
};

__attribute__((noinline)) static void inner() {
  Guard guard{1};
  exception.exception_cleanup = disposed;
  _Unwind_ForcedUnwind(&exception, stop, &observations);
  abort();
}

__attribute__((noinline)) static void outer() {
  Guard guard{4};
  try {
    inner();
  } catch (...) {
    assert(observations == 1);
    observations |= 2;
    throw;
  }
}

int main(int, char **) {
  outer();
  abort();
}
