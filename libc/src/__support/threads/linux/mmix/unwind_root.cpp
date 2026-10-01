//===-- MMIX Linux owning unwind activations ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "unwind_root.h"
#include "lifecycle.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "thread_finish.h"
#include <asm/rstack.h>
#include <asm/unistd.h>
#include <libunwind.h>
#include <unwind.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {
struct ForcedExit {
  _Unwind_Exception exception{};
  UnwindRoot root{};
  void *result = nullptr;
  bool active = false;
};
LIBC_CONSTINIT LIBC_THREAD_LOCAL ForcedExit forced;

uint64_t current_chain() {
  mmix_rstack_query query{};
  if (syscall_impl(__NR_mmix_rstack_query, reinterpret_cast<long>(&query)) ||
      !query.chain_id)
    __builtin_trap();
  return query.chain_id;
}

// The cursor must step to the caller, not infer its window from a fixed offset.
[[gnu::noinline, clang::disable_tail_calls]] void
capture_root(ThreadControl &control) {
  unw_context_t context;
  unw_cursor_t cursor;
  unw_proc_info_t procedure;
  unw_word_t ro, sp;
  if (unw_getcontext(&context) || unw_init_local(&cursor, &context) ||
      unw_step(&cursor) != 1 || unw_is_signal_frame(&cursor) != 0 ||
      unw_get_reg(&cursor, UNW_MMIX_RO, &ro) ||
      unw_get_reg(&cursor, UNW_REG_SP, &sp) ||
      unw_get_proc_info(&cursor, &procedure) ||
      procedure.start_ip !=
          reinterpret_cast<uintptr_t>(&run_with_unwind_root) ||
      !ro || !sp || (ro & 7) || (sp & 7))
    __builtin_trap();
  if (!control.retains_resources_until_process_exit() &&
      (sp < control.stack_bottom || sp >= control.stack_top))
    __builtin_trap();
  auto &root = control.unwind_root;
  if (root.active || root.generation == UINT64_MAX)
    __builtin_trap();
  ++root.generation;
  root.owner = &control;
  root.ro = ro;
  root.sp = sp;
  root.procedure = procedure.start_ip;
  root.chain = current_chain();
  root.active = true;
}

_Unwind_Reason_Code stop(int version, _Unwind_Action actions,
                         _Unwind_Exception_Class, _Unwind_Exception *,
                         _Unwind_Context *context, void *argument) {
  auto &state = *static_cast<ForcedExit *>(argument);
  auto *control = current_control;
  const auto &saved = state.root;
  if (version != 1 || !state.active || !control || saved.owner != control ||
      !control->unwind_root.active || control->unwind_root.owner != control ||
      saved.generation != control->unwind_root.generation ||
      saved.ro != control->unwind_root.ro ||
      saved.procedure != control->unwind_root.procedure ||
      saved.sp != control->unwind_root.sp ||
      saved.chain != control->unwind_root.chain ||
      saved.chain != current_chain() || (actions & _UA_END_OF_STACK) ||
      !(actions & _UA_FORCE_UNWIND))
    __builtin_trap();
  dispatch_c_cleanup(_Unwind_GetGR(context, UNW_MMIX_RO),
                     _Unwind_GetRegionStart(context), _Unwind_GetCFA(context));
  if (_Unwind_GetGR(context, UNW_MMIX_RO) == saved.ro &&
      _Unwind_GetRegionStart(context) == saved.procedure) {
    if (_Unwind_GetCFA(context) != saved.sp || has_c_cleanup())
      __builtin_trap();
    control->unwind_root.active = false;
    state.active = false;
    // Never return through consumed frames or release the current backing.
    exit_thread(*control, ThreadReturnValue(state.result));
  }
  return _URC_NO_REASON;
}

void disposed(_Unwind_Reason_Code, _Unwind_Exception *) { __builtin_trap(); }
} // namespace

[[gnu::noinline, clang::disable_tail_calls]] void
run_with_unwind_root(ThreadControl &control, RootBody *body, void *argument) {
  if (current_control != &control || !body)
    __builtin_trap();
  capture_root(control);
  body(argument);
  if (has_c_cleanup())
    __builtin_trap();
  control.unwind_root.active = false;
}

extern "C" [[noreturn]] void __llvm_libc_mmix_unwind_to_root(void *result) {
  auto *control = current_control;
  if (!control || !control->unwind_root.active || forced.active ||
      control->unwind_root.owner != control ||
      control->unwind_root.chain != current_chain())
    __builtin_trap();
  forced.root = control->unwind_root;
  forced.result = result;
  forced.active = true;
  forced.exception.exception_class = 0x4c4c564d4d4d4958ULL;
  forced.exception.exception_cleanup = disposed;
  _Unwind_ForcedUnwind(&forced.exception, stop, &forced);
  __builtin_trap();
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
