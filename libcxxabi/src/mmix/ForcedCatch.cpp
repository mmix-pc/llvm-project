//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "ForcedCatch.h"
#include <exception>
#include <stddef.h>

namespace __cxxabiv1 {
namespace mmix {
namespace {
// The selected libc has one terminal ForcedExit object per thread. Its
// LLVMPTHR exception is foreign: no native header may be read before it.
// Borrow the enclosing native catch head until this foreign catch ends.
// Fork copies this TLS state together with the suspended catch activations.
enum class Phase { Empty, Caught, Rethrown };
struct CatchState {
  __cxa_exception* parent = nullptr;
  __cxa_exception* foreign = nullptr;
  Phase phase = Phase::Empty;
  size_t intervening = 0;
};
constinit thread_local CatchState state;

void restore(__cxa_eh_globals* globals) {
  globals->caughtExceptions = state.parent;
  state = {};
}
} // namespace

bool begin_forced_catch(_Unwind_Exception* exception, __cxa_eh_globals* globals,
                        void*& result) {
  if (__getExceptionClass(exception) != UINT64_C(0x4c4c564d50544852)) {
    // Cleanup after a managed rethrow can itself catch another foreign
    // exception. Its rethrow also clears the generic head; distinguish that
    // end-catch from the suspended managed catch's end-catch.
    if (state.phase == Phase::Rethrown && !globals->caughtExceptions &&
        !__isOurExceptionClass(exception)) {
      if (state.intervening == SIZE_MAX)
        std::terminate();
      ++state.intervening;
    }
    return false;
  }
  if (state.phase != Phase::Empty)
    std::terminate();
  state.parent = globals->caughtExceptions;
  // Match the existing foreign-exception sentinel representation, without
  // accessing any field outside the real _Unwind_Exception object.
  state.foreign = reinterpret_cast<__cxa_exception*>(
      reinterpret_cast<char*>(exception) - offsetof(__cxa_exception, unwindHeader));
  state.phase = Phase::Caught;
  globals->caughtExceptions = state.foreign;
  result = exception + 1;
  return true;
}

bool end_forced_catch(__cxa_eh_globals* globals) {
  if (state.phase == Phase::Rethrown && state.intervening) {
    auto* head = globals->caughtExceptions;
    if (!head || !__isOurExceptionClass(&head->unwindHeader))
      --state.intervening;
    // Native catches above an intervening foreign catch still use their
    // ordinary handler counts. Neither kind ends the managed catch yet.
    return false;
  }
  if (state.phase == Phase::Rethrown && !globals->caughtExceptions) {
    // __cxa_rethrow cleared the foreign head. Do not let the generic end-catch
    // pop the parent: this call belongs to the rethrown foreign catch.
    restore(globals);
    return true;
  }
  if (state.phase == Phase::Caught && globals->caughtExceptions == state.foreign) {
    // Managed disposal traps on a swallowed forced exit. Preserve that call;
    // a normal returning cleanup, if supplied, still ends this catch once.
    _Unwind_DeleteException(&state.foreign->unwindHeader);
    restore(globals);
    return true;
  }
  // A native catch nested above this foreign catch uses the ordinary ABI
  // handler count and nextException link, including native rethrow.
  return false;
}

void rethrow_forced_catch(__cxa_eh_globals* globals) {
  if (state.phase == Phase::Caught && globals->caughtExceptions == state.foreign)
    state.phase = Phase::Rethrown;
  // The ordinary foreign branch clears the head and resumes the original
  // forced stop. Restore the borrowed parent only at its end-catch landing.
}
} // namespace mmix
} // namespace __cxxabiv1
