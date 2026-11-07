//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// REQUIRES: target={{.*-linux.*}}
// UNSUPPORTED: no-threads, no-exceptions

#undef NDEBUG
#include <assert.h>
#include <limits.h>
#include <libunwind.h>
#include <pthread.h>
#include <stdlib.h>
#include <unwind.h>

static _Unwind_Exception exception;
static bool destroyed;

struct Guard {
  __attribute__((noinline)) ~Guard() {
    assert(!destroyed);
    destroyed = true;
  }
};

__attribute__((noinline)) static void observe_nested_cursor() {
  unw_context_t context;
  unw_cursor_t cursor;
  assert(unw_getcontext(&context) == UNW_ESUCCESS);
  assert(unw_init_local(&cursor, &context) == UNW_ESUCCESS);
  assert(unw_step(&cursor) > 0);
}

static void disposed(_Unwind_Reason_Code, _Unwind_Exception *) { abort(); }

static _Unwind_Reason_Code stop(int version, _Unwind_Action actions,
                               _Unwind_Exception_Class,
                               _Unwind_Exception *object,
                               _Unwind_Context *, void *argument) {
  assert(version == 1 && object == &exception && argument == &exception);
  assert((actions & (_UA_FORCE_UNWIND | _UA_CLEANUP_PHASE)) ==
         (_UA_FORCE_UNWIND | _UA_CLEANUP_PHASE));
  assert(!(actions & _UA_END_OF_STACK));
  // A bounded nested cursor observation models the runtime's terminal-root
  // capture. This does not require arbitrary user callbacks to fit the minimum.
  if (!destroyed)
    return _URC_NO_REASON;
  observe_nested_cursor();
  _Exit(0);
}

static void *worker(void *) {
  Guard guard;
  exception.exception_cleanup = disposed;
  _Unwind_ForcedUnwind(&exception, stop, &exception);
  abort();
}

int main(int, char **) {
  pthread_attr_t attributes;
  assert(pthread_attr_init(&attributes) == 0);
  assert(pthread_attr_setstacksize(&attributes, PTHREAD_STACK_MIN) == 0);
  pthread_t thread;
  assert(pthread_create(&thread, &attributes, worker, nullptr) == 0);
  assert(pthread_attr_destroy(&attributes) == 0);
  pthread_join(thread, nullptr);
  abort();
}
