//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: no-exceptions, c++03
// VE only supports SjLj and does not provide _Unwind_ForcedUnwind.
// UNSUPPORTED: target={{ve-.*}}, target={{wasm.*}}

// Rethrowing a forced exception must resume its stop callback, not start a
// normal search that can terminate when no ordinary catch remains.
#include <assert.h>
#include <stdlib.h>
#include <tuple>
#include <unwind.h>

static _Unwind_Exception exception;
static int cleaned;
static bool rethrown;
static unsigned stops_after_rethrow;

struct Cleanup {
  ~Cleanup() { ++cleaned; }
};

template <typename T> struct Stop;
template <typename R, typename... Args> struct Stop<R (*)(Args...)> {
  typedef typename std::tuple_element<2, std::tuple<Args...>>::type Class;
  static _Unwind_Reason_Code stop(int version, _Unwind_Action actions, Class,
                                 _Unwind_Exception *object, _Unwind_Context *,
                                 void *argument) {
    assert(version == 1 && object == &exception && argument == &cleaned);
    assert((actions & (_UA_FORCE_UNWIND | _UA_CLEANUP_PHASE)) ==
           (_UA_FORCE_UNWIND | _UA_CLEANUP_PHASE));
    if (rethrown)
      ++stops_after_rethrow;
    if (actions & _UA_END_OF_STACK) {
      assert(rethrown && cleaned == 2 && stops_after_rethrow > 0);
      _Exit(0);
    }
    return _URC_NO_REASON;
  }
};

static void disposed(_Unwind_Reason_Code, _Unwind_Exception *) { abort(); }

int main(int, char **) {
  // Retain ordinary native rethrow behavior in the same runtime.
  try {
    try {
      throw 42;
    } catch (...) {
      throw;
    }
  } catch (int value) {
    assert(value == 42);
  }
  Cleanup outer;
  try {
    Cleanup inner;
    exception.exception_cleanup = disposed;
    _Unwind_ForcedUnwind(&exception, Stop<_Unwind_Stop_Fn>::stop, &cleaned);
    abort();
  } catch (...) {
    rethrown = true;
    throw;
  }
}
