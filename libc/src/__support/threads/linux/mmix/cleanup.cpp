//===-- MMIX Linux owning C cleanup stack --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "cleanup.h"
#include "lifecycle.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include <asm/rstack.h>
#include <asm/unistd.h>
#include <libunwind.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {
LIBC_CONSTINIT LIBC_THREAD_LOCAL MmixCleanupLink head{};
LIBC_CONSTINIT LIBC_THREAD_LOCAL uint64_t serial = 0;
LIBC_CONSTINIT LIBC_THREAD_LOCAL bool dispatching = false;

MmixCleanupFrame owner_frame(uintptr_t ro, uintptr_t procedure, uintptr_t sp) {
  auto *control = current_control;
  mmix_rstack_query query{};
  if (!control || !control->unwind_root.active ||
      control->unwind_root.owner != control || !ro || !procedure || !sp ||
      (ro & 7) || (sp & 7) ||
      syscall_impl(__NR_mmix_rstack_query, reinterpret_cast<long>(&query)) ||
      query.chain_id != control->unwind_root.chain ||
      ro < control->unwind_root.ro ||
      (!control->retains_resources_until_process_exit() &&
       (sp < control->stack_bottom || sp >= control->stack_top)))
    __builtin_trap();
  return {control,
          ro,
          procedure,
          sp,
          0,
          query.chain_id,
          control->unwind_root.generation};
}

bool same_frame(const MmixCleanupFrame &a, const MmixCleanupFrame &b) {
  return a.owner == b.owner && a.ro == b.ro && a.procedure == b.procedure &&
         a.sp == b.sp && a.chain == b.chain && a.generation == b.generation;
}

// Step past capture and push/pop to the lexical owner. Validate record storage
// against its frame interval before reading it, including on the main stack.
[[gnu::noinline, clang::disable_tail_calls]] MmixCleanupFrame
capture(MmixCleanupRecord *record) {
  unw_context_t context;
  unw_cursor_t cursor;
  unw_proc_info_t procedure;
  unw_word_t ro, sp, parent_sp;
  if (unw_getcontext(&context) || unw_init_local(&cursor, &context) ||
      unw_step(&cursor) != 1 || unw_is_signal_frame(&cursor) != 0 ||
      unw_step(&cursor) != 1 || unw_is_signal_frame(&cursor) != 0 ||
      unw_get_reg(&cursor, UNW_MMIX_RO, &ro) ||
      unw_get_reg(&cursor, UNW_REG_SP, &sp) ||
      unw_get_proc_info(&cursor, &procedure) || unw_step(&cursor) != 1 ||
      unw_is_signal_frame(&cursor) != 0 ||
      unw_get_reg(&cursor, UNW_REG_SP, &parent_sp))
    __builtin_trap();
  uintptr_t address = reinterpret_cast<uintptr_t>(record);
  if (address < sp || address > parent_sp ||
      parent_sp - address < sizeof(*record) || (address & 7))
    __builtin_trap();
  auto frame = owner_frame(ro, procedure.start_ip, sp);
  frame.limit = parent_sp;
  return frame;
}

void validate_link(const MmixCleanupLink &link) {
  uintptr_t address = reinterpret_cast<uintptr_t>(link.record);
  if (!link.serial || !link.record || (address & 7) ||
      address < link.frame.sp || address > link.frame.limit ||
      link.frame.limit - address < sizeof(*link.record) ||
      link.record->serial != link.serial || !link.record->callback)
    __builtin_trap();
}

void remove(bool execute) {
  validate_link(head);
  auto *record = head.record;
  if (record->previous.record && record->previous.serial >= head.serial)
    __builtin_trap();
  auto callback = record->callback;
  auto argument = record->argument;
  head = record->previous;
  record->serial = 0;
  if (execute)
    callback(argument);
}
} // namespace

extern "C" [[gnu::noinline, clang::disable_tail_calls]] void
__llvm_libc_mmix_thread_cleanup_push(MmixCleanupRecord *record,
                                     void (*callback)(void *), void *argument) {
  auto frame = capture(record);
  if (!callback || dispatching || serial == UINT64_MAX ||
      (head.record && (head.record == record || head.frame.ro > frame.ro)))
    __builtin_trap();
  // Serial ordering bounds the walk and rejects cycles before reusing storage.
  uint64_t newer = serial + 1;
  for (auto link = head; link.record; link = link.record->previous) {
    if (link.record == record || link.serial >= newer ||
        link.frame.owner != frame.owner || link.frame.chain != frame.chain ||
        link.frame.generation != frame.generation || link.frame.ro > frame.ro)
      __builtin_trap();
    validate_link(link);
    newer = link.serial;
  }
  // Do not read uninitialized automatic record storage.
  *record = {head, callback, argument, ++serial};
  head = {record, frame, serial};
}

extern "C" [[gnu::noinline, clang::disable_tail_calls]] void
__llvm_libc_mmix_thread_cleanup_pop(MmixCleanupRecord *record, int execute) {
  auto frame = capture(record);
  if (dispatching || head.record != record || !same_frame(head.frame, frame))
    __builtin_trap();
  remove(execute != 0);
}

void dispatch_c_cleanup(uintptr_t ro, uintptr_t procedure, uintptr_t sp) {
  auto frame = owner_frame(ro, procedure, sp);
  if (dispatching)
    __builtin_trap();
  dispatching = true;
  while (head.record) {
    if (head.frame.owner != frame.owner || head.frame.chain != frame.chain ||
        head.frame.generation != frame.generation || head.frame.ro > ro)
      __builtin_trap();
    if (head.frame.ro != ro)
      break;
    if (!same_frame(head.frame, frame))
      __builtin_trap();
    remove(true);
  }
  dispatching = false;
}

bool has_c_cleanup() { return head.record != nullptr; }
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
