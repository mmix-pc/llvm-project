//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

// Exercise the DWARF consumers with failing capture/cursor providers, without
// linking a platform unwinder or relying on an actual machine capture failure.
// REQUIRES: target={{x86_64-.*linux.*}}
// RUN: %{cxx} %s -I%S/../include -fno-exceptions -o %t.exe
// RUN: %{exec} %t.exe

#define NDEBUG
#include "../src/config.h"
#include <setjmp.h>
static jmp_buf abortTarget;
#undef _LIBUNWIND_ABORT
#define _LIBUNWIND_ABORT(message) longjmp(abortTarget, 1)
#include "../src/UnwindLevel1.c"
#include "../src/UnwindLevel1-gcc-ext.c"
#undef NDEBUG
#include <assert.h>

enum Failure { Capture, Init, SetReg, ProcInfo, None };
static Failure failure;
static int captures, inits, sets, infos, steps;
static void reset(Failure f) {
  failure = f;
  captures = inits = sets = infos = steps = 0;
}
extern "C" int __unw_getcontext(unw_context_t *) {
  ++captures;
  return failure == Capture ? UNW_EUNSPEC : UNW_ESUCCESS;
}
extern "C" int __unw_init_local(unw_cursor_t *, unw_context_t *) {
  assert(failure != Capture);
  ++inits;
  return failure == Init ? UNW_EINVAL : UNW_ESUCCESS;
}
extern "C" int __unw_set_reg(unw_cursor_t *, unw_regnum_t, unw_word_t) {
  assert(failure != Capture && failure != Init);
  ++sets;
  return failure == SetReg ? UNW_EBADREG : UNW_ESUCCESS;
}
extern "C" int __unw_get_proc_info(unw_cursor_t *, unw_proc_info_t *info) {
  assert(failure != Capture && failure != Init && failure != SetReg);
  ++infos;
  if (failure == ProcInfo)
    return UNW_ENOINFO;
  memset(info, 0, sizeof(*info));
  info->start_ip = 0x1000;
  info->unwind_info = 0x2000;
  info->extra = 0x3000;
  return UNW_ESUCCESS;
}
extern "C" int __unw_step(unw_cursor_t *) {
  assert(failure == None);
  ++steps;
  return 0;
}
extern "C" int __unw_step_stage2(unw_cursor_t *cursor) {
  return __unw_step(cursor);
}
extern "C" int __unw_get_reg(unw_cursor_t *, unw_regnum_t, unw_word_t *) {
  abort();
}
extern "C" int __unw_resume_with_frames_walked(unw_cursor_t *, unsigned) {
  abort();
}
extern "C" int __unw_is_signal_frame(unw_cursor_t *) { abort(); }
extern "C" void __unw_add_dynamic_fde(unw_word_t) { abort(); }
extern "C" void __unw_remove_dynamic_fde(unw_word_t) { abort(); }

static _Unwind_Reason_Code trace(_Unwind_Context *, void *) { abort(); }
static _Unwind_Reason_Code stop(int, _Unwind_Action, uint64_t,
                               _Unwind_Exception *, _Unwind_Context *, void *) {
  abort();
}

int main() {
  _Unwind_Exception exception = {};
  const Failure contextFailures[] = {Capture, Init};
  for (Failure f : contextFailures) {
    reset(f);
    exception.private_1 = 42;
    exception.private_2 = 43;
    assert(_Unwind_RaiseException(&exception) == _URC_FATAL_PHASE1_ERROR);
    assert(steps == 0 && inits == (f == Init));
    if (f == Capture)
      assert(exception.private_1 == 42 && exception.private_2 == 43);
    reset(f);
    exception.private_1 = 42;
    exception.private_2 = 43;
    assert(_Unwind_ForcedUnwind(&exception, stop, nullptr) ==
           _URC_FATAL_PHASE2_ERROR);
    assert(steps == 0 && inits == (f == Init));
    if (f == Capture)
      assert(exception.private_1 == 42 && exception.private_2 == 43);
    reset(f);
    assert(_Unwind_Backtrace(trace, nullptr) == _URC_FATAL_PHASE1_ERROR);
    assert(steps == 0 && inits == (f == Init));
    for (int forced = 0; forced != 2; ++forced) {
      reset(f);
      exception.private_1 = forced ? (uintptr_t)stop : 0;
      if (setjmp(abortTarget) == 0) {
        _Unwind_Resume(&exception);
        abort();
      }
      assert(captures == 1 && inits == (f == Init) && steps == 0);
    }
  }
  const Failure lookupFailures[] = {Capture, Init, SetReg, ProcInfo};
  for (Failure f : lookupFailures) {
    reset(f);
    assert(_Unwind_FindEnclosingFunction((void *)0x1234) == nullptr);
    assert(inits == (f != Capture) && sets == (f >= SetReg));
    reset(f);
    dwarf_eh_bases bases = {1, 2, 3};
    assert(_Unwind_Find_FDE((void *)0x1234, &bases) == nullptr);
    assert(bases.tbase == 0 && bases.dbase == 0 && bases.func == 0);
    assert(inits == (f != Capture) && sets == (f >= SetReg));
    assert(infos == (f == ProcInfo) && steps == 0);
  }
  reset(None);
  dwarf_eh_bases bases = {};
  assert(_Unwind_FindEnclosingFunction((void *)0x1234) == (void *)0x1000);
  assert(_Unwind_Find_FDE((void *)0x1234, &bases) == (void *)0x2000);
  assert(bases.func == 0x1000 && bases.tbase == 0x3000 && bases.dbase == 0);
  assert(_Unwind_RaiseException(&exception) == _URC_END_OF_STACK);
  assert(_Unwind_Backtrace(trace, nullptr) == _URC_END_OF_STACK);
}
