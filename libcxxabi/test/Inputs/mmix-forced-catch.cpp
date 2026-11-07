//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
// Selected MMIX managed-catch adapter integration input. Execute against the
// actual ABI and unwinder with _LIBCXXABI_MMIX_FORCED_CATCH enabled; ordinary
// host ABI builds intentionally reject the nested managed foreign catch.
// No arguments selects one native parent; any argument selects two.
#include "../../src/cxa_exception.h"
#include <assert.h>
#include <stdlib.h>
#include <unwind.h>
static _Unwind_Exception exception;
static unsigned destroyed, guards, catches, stops, depth;
struct Native { ~Native() { ++destroyed; } };
struct Guard { ~Guard() { ++guards; } };
static void disposed(_Unwind_Reason_Code, _Unwind_Exception*) { abort(); }
static _Unwind_Reason_Code stop(int version, _Unwind_Action actions,
    uint64_t, _Unwind_Exception *object, _Unwind_Context*, void*) {
  assert(version == 1 && object == &exception);
  assert((actions & (_UA_FORCE_UNWIND | _UA_CLEANUP_PHASE)) == (_UA_FORCE_UNWIND | _UA_CLEANUP_PHASE));
  ++stops;
  if (actions & _UA_END_OF_STACK) {
    assert(destroyed == depth && guards == 2 && catches == 2 && stops > 2);
    auto *g = __cxxabiv1::__cxa_get_globals();
    assert(!g->caughtExceptions && !g->uncaughtExceptions);
    _Exit(0);
  }
  return _URC_NO_REASON;
}
static _Unwind_Exception other;
static unsigned other_deleted;
static void delete_other(_Unwind_Reason_Code, _Unwind_Exception*) { ++other_deleted; }
[[noreturn]] __attribute__((noinline)) static void raise_other() {
  other = {};
  other.exception_class = 0x1122334455667788ULL;
  other.exception_cleanup = delete_other;
  _Unwind_RaiseException(&other);
  abort();
}
struct DuringRethrow {
  ~DuringRethrow() {
    assert(!__cxxabiv1::__cxa_get_globals()->caughtExceptions);
    try { try { raise_other(); } catch (...) { throw; } }
    catch (...) { }
    assert(other_deleted == 1);
    assert(!__cxxabiv1::__cxa_get_globals()->caughtExceptions);
  }
};
[[noreturn]] static void raise() {
  Guard guard;
  try {
    Guard inner;
    exception.exception_class = 0x4c4c564d50544852ULL;
    exception.exception_cleanup = disposed;
    _Unwind_ForcedUnwind(&exception, stop, nullptr);
    abort();
  } catch (...) {
    assert(destroyed == 0 && guards == 1);
    ++catches;
    DuringRethrow pending;
    try { try { throw 7; } catch (int n) { assert(n == 7); throw; } }
    catch (int n) { assert(n == 7); }
    assert(!__cxxabiv1::__cxa_current_exception_type());
    throw;
  }
}
[[noreturn]] __attribute__((noinline)) static void frame(unsigned n) {
  try { throw Native{}; }
  catch (Native&) {
    if (n > 1) frame(n - 1);
    try { raise(); }
    catch (...) { ++catches; assert(!destroyed && guards == 2); throw; }
  }
  abort();
}
int main(int argc, char**) {
  depth = argc == 1 ? 1 : 2;
  frame(depth);
}
