//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Deterministic phase2/API control-flow tests with cursor/restore providers.
// These do not execute a machine landing pad or a language personality.
// REQUIRES: target={{x86_64-.*linux.*}}
// RUN: %{cxx} %s -I%S/../include -fno-exceptions -o %t.exe
// RUN: %{exec} %t.exe
// RUN: %{cxx} %s -I%S/../include -fno-exceptions -DTEST_UNSUPPORTED_BACKEND -o %t.unsupported.exe
// RUN: %{exec} %t.unsupported.exe
// RUN: %{cxx} %s -I%S/../include -fno-exceptions -fcf-protection=full -mshstk -o %t.cet.exe
// RUN: %{exec} %t.cet.exe

#define NDEBUG
#define _LIBUNWIND_IS_NATIVE_ONLY
#include "../src/config.h"
#ifdef TEST_UNSUPPORTED_BACKEND
#define _LIBUNWIND_SUPPORT_COMPACT_UNWIND 1
#endif
#include <setjmp.h>
static jmp_buf abortTarget, installTarget;
#undef _LIBUNWIND_ABORT
#define _LIBUNWIND_ABORT(message) longjmp(abortTarget, 1)
#include "../src/UnwindLevel1.c"
#include "../src/UnwindLevel1-gcc-ext.c"
#undef NDEBUG
#include <assert.h>

enum Failure { None, Init, Info, Stop, Step, Personality, Restore };
static Failure failure;
static int captures, inits, currentInits, steps, restores, captureFrame;
static int installFrame, events[32], eventCount;
static bool leaf;
static unsigned restoredCount;
static _Unwind_Exception exception;
static int parameter;

static void event(int kind, int frame) {
  assert(eventCount < 32);
  events[eventCount++] = kind * 100 + frame;
}
static void reset(Failure f = None) {
  failure = f;
  captures = inits = currentInits = steps = restores = eventCount = 0;
  captureFrame = 0;
  installFrame = -1;
  leaf = false;
  restoredCount = ~0u;
  exception = {};
}
static void expect(const int *values, int count) {
  assert(eventCount == count);
  for (int i = 0; i != count; ++i)
    assert(events[i] == values[i]);
}

extern "C" int __unw_getcontext(unw_context_t *context) {
  ++captures;
  context->data[0] = captureFrame;
  return UNW_ESUCCESS;
}
extern "C" int __unw_init_local(unw_cursor_t *cursor, unw_context_t *context) {
  ++inits;
  cursor->data[0] = context->data[0];
  cursor->data[1] = 0;
  return failure == Init ? UNW_EINVAL : UNW_ESUCCESS;
}
extern "C" int __unw_init_local_current(unw_cursor_t *cursor,
                                        unw_context_t *context) {
  ++currentInits;
  cursor->data[0] = context->data[0];
  cursor->data[1] = 1;
  return failure == Init ? UNW_EINVAL : UNW_ESUCCESS;
}
extern "C" int __unw_get_reg(unw_cursor_t *cursor, unw_regnum_t reg,
                             unw_word_t *value) {
  *value = reg == UNW_REG_IP ? 0x1000 + cursor->data[0] : 0x2000;
  return UNW_ESUCCESS;
}
extern "C" int __unw_set_reg(unw_cursor_t *cursor, unw_regnum_t reg,
                             unw_word_t) {
  if (reg == UNW_REG_IP)
    cursor->data[1] = 0;
  return UNW_ESUCCESS;
}
extern "C" int __unw_is_ip_before_instruction(unw_cursor_t *cursor) {
  return cursor->data[1];
}
extern "C" int __unw_is_signal_frame(unw_cursor_t *) { return 0; }
extern "C" int __unw_step_stage2(unw_cursor_t *cursor) {
  ++steps;
  event(1, cursor->data[0]);
  if (failure == Step)
    return UNW_EBADFRAME;
  if (cursor->data[0] == 2 || cursor->data[0] == 12)
    return UNW_STEP_END;
  ++cursor->data[0];
  cursor->data[1] = 0;
  return UNW_STEP_SUCCESS;
}
extern "C" int __unw_step(unw_cursor_t *cursor) {
  return __unw_step_stage2(cursor);
}
static _Unwind_Reason_Code personality(int version, _Unwind_Action actions,
                                       uint64_t, _Unwind_Exception *ex,
                                       _Unwind_Context *context) {
  assert(version == 1 && ex == &exception);
  assert(actions == (_UA_FORCE_UNWIND | _UA_CLEANUP_PHASE));
  int frame = ((unw_cursor_t *)context)->data[0];
  event(3, frame);
  if (failure == Personality)
    return _URC_FATAL_PHASE2_ERROR;
  if (frame == installFrame) {
    _Unwind_SetIP(context, 0x3000);
    return _URC_INSTALL_CONTEXT;
  }
  return _URC_CONTINUE_UNWIND;
}
extern "C" int __unw_get_proc_info(unw_cursor_t *cursor, unw_proc_info_t *info) {
  if (failure == Info)
    return UNW_ENOINFO;
  memset(info, 0, sizeof(*info));
  info->start_ip = 0x1000;
  info->end_ip = 0x4000;
  info->handler = leaf && cursor->data[0] == 1 ? 0 : (unw_word_t)personality;
  return UNW_ESUCCESS;
}
extern "C" int __unw_resume_with_frames_walked(unw_cursor_t *cursor,
                                               unsigned count) {
  ++restores;
  restoredCount = count;
  assert(cursor->data[1] == 0);
  if (failure == Restore)
    return UNW_EUNSPEC;
  longjmp(installTarget, 1);
}
void *__libunwind_shstk_get_registers(unw_cursor_t *) { abort(); }
void *__libunwind_shstk_get_jump_target() { abort(); }
extern "C" void __unw_add_dynamic_fde(unw_word_t) { abort(); }
extern "C" void __unw_remove_dynamic_fde(unw_word_t) { abort(); }

static _Unwind_Reason_Code stop(int version, _Unwind_Action actions, uint64_t,
                                _Unwind_Exception *ex, _Unwind_Context *context,
                                void *arg) {
  assert(version == 1 && ex == &exception && arg == &parameter);
  assert(actions & _UA_FORCE_UNWIND);
  assert(actions & _UA_CLEANUP_PHASE);
  assert(ex->private_1 == (uintptr_t)stop);
  assert(ex->private_2 == (uintptr_t)&parameter);
  unw_cursor_t *cursor = (unw_cursor_t *)context;
  int before = -1;
  assert(_Unwind_GetIPInfo(context, &before) == 0x1000 + cursor->data[0]);
  assert(before == (int)cursor->data[1]);
  event(actions & _UA_END_OF_STACK ? 4 : 2, cursor->data[0]);
  return failure == Stop ? _URC_NORMAL_STOP : _URC_NO_REASON;
}

int main() {
  unw_context_t seed = {};
  seed.data[0] = 1;
#if !defined(_LIBUNWIND_SUPPORT_SAVED_CONTEXT)
  reset();
  exception.private_1 = 42;
  exception.private_2 = 43;
  // An unsupported entry must not even dereference the supplied context.
  assert(__unw_forcedunwind_from_context(&exception, stop, &parameter,
                                        (unw_context_t *)1) ==
         _URC_FATAL_PHASE2_ERROR);
  assert(exception.private_1 == 42 && exception.private_2 == 43);
  assert(captures == 0 && inits == 0 && currentInits == 0 && steps == 0);
  assert(eventCount == 0 && restores == 0);
#else
  reset();
  exception.private_1 = 42;
  exception.private_2 = 43;
  assert(__unw_forcedunwind_from_context(nullptr, stop, &parameter, &seed) ==
         _URC_FATAL_PHASE2_ERROR);
  assert(__unw_forcedunwind_from_context(&exception, nullptr, &parameter, &seed) ==
         _URC_FATAL_PHASE2_ERROR);
  assert(__unw_forcedunwind_from_context(&exception, stop, &parameter, nullptr) ==
         _URC_FATAL_PHASE2_ERROR);
  assert(exception.private_1 == 42 && exception.private_2 == 43);
  assert(captures == 0 && currentInits == 0 && eventCount == 0);

  reset();
  assert(__unw_forcedunwind_from_context(&exception, stop, &parameter, &seed) ==
         _URC_FATAL_PHASE2_ERROR);
  const int saved[] = {201, 301, 101, 202, 302, 102, 402};
  expect(saved, 7);
  assert(captures == 0 && inits == 0 && currentInits == 1 && steps == 2);
  assert(seed.data[0] == 1 && seed.data[1] == 0);

  reset();
  leaf = true;
  assert(__unw_forcedunwind_from_context(&exception, stop, &parameter, &seed) ==
         _URC_FATAL_PHASE2_ERROR);
  const int noPersonality[] = {201, 101, 202, 302, 102, 402};
  expect(noPersonality, 6);

  reset();
  assert(_Unwind_ForcedUnwind(&exception, stop, &parameter) ==
         _URC_FATAL_PHASE2_ERROR);
  const int ordinary[] = {100, 201, 301, 101, 202, 302, 102, 402};
  expect(ordinary, 8);
  assert(captures == 1 && inits == 1 && currentInits == 0);

  const Failure failures[] = {Init, Info, Stop, Step, Personality, Restore};
  for (Failure f : failures) {
    reset(f);
    installFrame = f == Restore ? 1 : -1;
    assert(__unw_forcedunwind_from_context(&exception, stop, &parameter, &seed) ==
           _URC_FATAL_PHASE2_ERROR);
    assert(captures == 0 && inits == 0 && currentInits == 1);
    for (int i = 0; i != eventCount; ++i)
      assert(events[i] / 100 != 4);
    assert(steps == (f == Step));
    assert(restores == (f == Restore));
    if (f == Restore)
      assert(restoredCount == 0);
  }

  // Installation abandons the initial activation. Invoke the real resume APIs
  // with a provider-supplied landing-pad capture, not the original seed.
  for (int rethrow = 0; rethrow != 2; ++rethrow) {
    reset();
    installFrame = 1;
    if (setjmp(installTarget) == 0) {
      __unw_forcedunwind_from_context(&exception, stop, &parameter, &seed);
      abort();
    }
    assert(restoredCount == 0 && steps == 0 && currentInits == 1);
    captureFrame = 10;
    installFrame = 12;
    eventCount = 0;
    if (setjmp(installTarget) == 0) {
      if (rethrow)
        _Unwind_Resume_or_Rethrow(&exception);
      else
        _Unwind_Resume(&exception);
      abort();
    }
    const int resumed[] = {110, 211, 311, 111, 212, 312};
    expect(resumed, 6);
    assert(captures == 1 && inits == 1 && currentInits == 1);
    assert(restoredCount == 3 && restores == 2);
  }

  // Preserve the ordinary path's legacy error/end and returned-install behavior.
  reset(Step);
  assert(_Unwind_ForcedUnwind(&exception, stop, &parameter) ==
         _URC_FATAL_PHASE2_ERROR);
  const int stepError[] = {100, 400};
  expect(stepError, 2);
  reset(Restore);
  installFrame = 1;
  assert(_Unwind_ForcedUnwind(&exception, stop, &parameter) ==
         _URC_FATAL_PHASE2_ERROR);
  expect(ordinary, 8);
  assert(restoredCount == 2);

  reset(Init);
  exception.private_1 = (uintptr_t)stop;
  exception.private_2 = (uintptr_t)&parameter;
  if (setjmp(abortTarget) == 0) {
    _Unwind_Resume(&exception);
    abort();
  }
  assert(captures == 1 && inits == 1 && currentInits == 0 && steps == 0);
#endif
}
