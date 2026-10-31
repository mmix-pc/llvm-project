//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Real native cursor/parser, controlled registered FDEs and saved registers.
// No machine restore is executed. Include the provider to test hidden helpers
// without depending on whether the installed test library is static or shared.
// REQUIRES: target={{x86_64-.*linux.*}}
// RUN: %{cxx} %s -I%S/../include -fno-exceptions -fno-rtti -pthread -ldl -o %t.exe
// RUN: %{exec} %t.exe
// RUN: %{cxx} %s -I%S/../include -fno-exceptions -fno-rtti -pthread -ldl -fcf-protection=full -mshstk -o %t.cet.exe
// RUN: %{exec} %t.cet.exe

#define NDEBUG
#define _LIBUNWIND_IS_NATIVE_ONLY
#include "../src/libunwind.cpp"
#undef NDEBUG
#include <assert.h>

namespace libunwind {
extern "C" void __libunwind_Registers_x86_64_jumpto(Registers_x86_64 *) {
  abort();
}
} // namespace libunwind

using Cursor = libunwind::UnwindCursor<libunwind::LocalAddressSpace,
                                     libunwind::Registers_x86_64>;
struct LegacyCursorLayout : libunwind::AbstractUnwindCursor {
  libunwind::LocalAddressSpace &addressSpace;
  libunwind::Registers_x86_64 registers;
  unw_proc_info_t info;
  bool unwindInfoMissing;
  bool isSignalFrame;
};
static_assert(sizeof(Cursor) == sizeof(LegacyCursorLayout), "cursor grew");
static_assert(alignof(Cursor) == alignof(LegacyCursorLayout), "alignment changed");
static_assert(sizeof(Cursor) == sizeof(unw_cursor_t), "native storage changed");

struct Bytes {
  unsigned char data[256] = {};
  size_t size = 0;

  void byte(unsigned char value) { data[size++] = value; }
  void word(uint32_t value) {
    memcpy(data + size, &value, sizeof(value));
    size += sizeof(value);
  }
  void pointer(uintptr_t value) {
    memcpy(data + size, &value, sizeof(value));
    size += sizeof(value);
  }
  void endRecord(size_t start) {
    while (size % 8)
      byte(DW_CFA_nop);
    uint32_t length = size - start - 4;
    memcpy(data + start, &length, sizeof(length));
    assert(size <= sizeof(data));
  }
  size_t fde(uintptr_t pc) {
    size_t start = size;
    word(0);
    word(size); // Distance from this CIE-pointer field to the CIE at offset 0.
    pointer(pc);
    pointer(8);
    return start;
  }
  void row(unsigned cfaOffset, unsigned argsSize) {
    byte(DW_CFA_def_cfa_offset);
    byte(cfaOffset);
    byte(DW_CFA_GNU_args_size);
    byte(argsSize);
  }
};

static unw_context_t context(uintptr_t pc, uintptr_t sp) {
  libunwind::Registers_x86_64 registers;
  registers.setIP(pc);
  registers.setSP(sp);
  unw_context_t result = {};
  static_assert(sizeof(registers) <= sizeof(result), "register storage");
  memcpy(&result, &registers, sizeof(registers));
  return result;
}

static uintptr_t reg(unw_cursor_t &cursor, unw_regnum_t which) {
  unw_word_t value;
  assert(__unw_get_reg(&cursor, which, &value) == UNW_ESUCCESS);
  return value;
}

int main() {
#if !defined(_LIBUNWIND_SUPPORT_SAVED_CONTEXT)
  unw_cursor_t cursor;
  memset(&cursor, 0x5a, sizeof(cursor));
  unw_cursor_t original = cursor;
  assert(__unw_init_local_current(&cursor, (unw_context_t *)1) == UNW_EINVAL);
  assert(memcmp(&cursor, &original, sizeof(cursor)) == 0);
  return 0;
#else
  Bytes eh;
  eh.word(0);
  eh.word(0); // CIE id.
  eh.byte(1); // Version.
  eh.byte(0); // Empty augmentation, absolute FDE PCs.
  eh.byte(1); // Code alignment.
  eh.byte(0x78); // SLEB128(-8), data alignment.
  eh.byte(16); // x86-64 return address register.
  eh.byte(DW_CFA_def_cfa);
  eh.byte(7); // RSP.
  eh.byte(8);
  eh.byte(DW_CFA_offset | 16);
  eh.byte(1); // Return address at CFA - 8.
  eh.endRecord(0);
  size_t first = eh.fde(0x1000);
  eh.row(16, 3); // Rules beginning exactly at the FDE start.
  eh.byte(DW_CFA_advance_loc | 4);
  eh.row(24, 5); // Rules beginning exactly at 0x1004.
  eh.endRecord(first);
  size_t second = eh.fde(0x1008);
  eh.row(32, 7);
  eh.endRecord(second);
  eh.word(0);
  __unw_add_dynamic_fde((uintptr_t)(eh.data + first));
  __unw_add_dynamic_fde((uintptr_t)(eh.data + second));

  uintptr_t stack[] = {0x100d, 0x100d, 0x100d, 0x100d, 0};
  uintptr_t sp = (uintptr_t)stack;
  struct Case { uintptr_t pc, start, cfaOffset, argsSize; };
  const Case cases[] = {{0x1000, 0x1000, 16, 3}, {0x1004, 0x1000, 24, 5},
                        {0x1007, 0x1000, 24, 5}, {0x1008, 0x1008, 32, 7}};
  for (Case c : cases) {
    unw_context_t seed = context(c.pc, sp);
    unw_context_t original = seed;
    unw_cursor_t cursor;
    assert(__unw_init_local_current(&cursor, &seed) == UNW_ESUCCESS);
    assert(memcmp(&seed, &original, sizeof(seed)) == 0);
    assert(reg(cursor, UNW_REG_IP) == c.pc);
    assert(__unw_is_ip_before_instruction(&cursor) == 1);
    assert(__unw_is_signal_frame(&cursor) == 0);
    unw_proc_info_t info;
    assert(__unw_get_proc_info(&cursor, &info) == UNW_ESUCCESS);
    assert(info.start_ip == c.start && info.end_ip == c.start + 8);
    assert(info.gp == c.argsSize);
    assert(__unw_set_reg(&cursor, UNW_X86_64_RAX, 99) == UNW_ESUCCESS);
    assert(__unw_is_ip_before_instruction(&cursor) == 1);

    unw_cursor_t copy;
    assert(__unw_copy_cursor(&copy, &cursor) == UNW_ESUCCESS);
    assert(__unw_is_ip_before_instruction(&copy) == 1);
    assert(__unw_step_stage2(&copy) == UNW_STEP_SUCCESS);
    assert(reg(copy, UNW_REG_SP) == sp + c.cfaOffset);
    assert(reg(copy, UNW_REG_IP) == 0x100d);
    assert(__unw_is_ip_before_instruction(&copy) == 0);
    assert(__unw_is_ip_before_instruction(&cursor) == 1);
    __unw_destroy_cursor(&copy);

    assert(__unw_set_reg(&cursor, UNW_REG_IP, 0x1004) == UNW_ESUCCESS);
    assert(reg(cursor, UNW_REG_IP) == 0x1004);
    assert(__unw_is_ip_before_instruction(&cursor) == 0);
    assert(__unw_get_proc_info(&cursor, &info) == UNW_ESUCCESS);
    assert(info.gp == 3); // Ordinary parser convention remains unchanged.
    __unw_destroy_cursor(&cursor);
  }

  unw_context_t seed = context(0x1004, sp);
  unw_cursor_t ordinary;
  assert(__unw_init_local(&ordinary, &seed) == UNW_ESUCCESS);
  assert(__unw_is_ip_before_instruction(&ordinary) == 0);
  unw_proc_info_t info;
  assert(__unw_get_proc_info(&ordinary, &info) == UNW_ESUCCESS);
  assert(info.gp == 3);
  assert(__unw_step_stage2(&ordinary) == UNW_STEP_SUCCESS);
  assert(reg(ordinary, UNW_REG_SP) == sp + 16);
  __unw_destroy_cursor(&ordinary);

  // Missing initial metadata is not a successful step and must retain the bit.
  seed = context(0x4000, sp);
  unw_cursor_t missing;
  assert(__unw_init_local_current(&missing, &seed) == UNW_ESUCCESS);
  assert(__unw_get_proc_info(&missing, &info) == UNW_ENOINFO);
  assert(__unw_step_stage2(&missing) == UNW_STEP_END);
  assert(__unw_is_ip_before_instruction(&missing) == 1);
  __unw_destroy_cursor(&missing);

  const uintptr_t invalidPCs[] = {0, UINTPTR_MAX};
  for (uintptr_t pc : invalidPCs) {
    seed = context(pc, sp);
    unw_cursor_t invalid;
    assert(__unw_init_local_current(&invalid, &seed) == UNW_EINVAL);
    __unw_destroy_cursor(&invalid);
  }
  assert(__unw_init_local_current(nullptr, &seed) == UNW_EINVAL);
  assert(__unw_init_local_current(&missing, nullptr) == UNW_EINVAL);
  __unw_remove_dynamic_fde((uintptr_t)(eh.data + second));
  __unw_remove_dynamic_fde((uintptr_t)(eh.data + first));
#endif
}
