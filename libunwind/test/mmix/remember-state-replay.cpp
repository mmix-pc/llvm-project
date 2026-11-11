//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
#define NDEBUG
#include "AddressSpace.hpp"
#include "DwarfParser.hpp"
#include <stdlib.h>
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
static bool denyAllocation;
static unsigned denied;
extern "C" void *__wrap_malloc(size_t size) {
  if (denyAllocation) {
    ++denied;
    return nullptr;
  }
  return __real_malloc(size);
}
extern "C" void __wrap_free(void *p) { __real_free(p); }
using Parser = libunwind::CFI_Parser<libunwind::LocalAddressSpace>;
#if defined(_LIBUNWIND_TARGET_MMIX)
using Registers = libunwind::Registers_mmix;
#else
using Registers = libunwind::Registers_x86_64;
#endif

static Parser::PrologInfo parse(const uint8_t *cieBytes, size_t cieSize,
                                const uint8_t *fdeBytes, size_t fdeSize,
                                uintptr_t offset, bool expected = true) {
  Parser::CIE_Info cie = {};
  cie.cieStart = cie.cieInstructions = reinterpret_cast<uintptr_t>(cieBytes);
  cie.cieLength = cieSize;
  cie.codeAlignFactor = 1;
  cie.dataAlignFactor = -8;
  Parser::FDE_Info fde = {};
  fde.fdeStart = fde.fdeInstructions = reinterpret_cast<uintptr_t>(fdeBytes);
  fde.fdeLength = fdeSize;
  fde.pcStart = 0x1000;
  fde.pcEnd = 0x10000;
  libunwind::LocalAddressSpace space;
  Parser::PrologInfo state;
  denyAllocation = true;
  bool ok = Parser::parseCFIWithoutAllocation(space, fde, cie, 0x1000 + offset,
                                              Registers::getArch(), &state);
  denyAllocation = false;
  assert(ok == expected && denied == 0);
  return state;
}

int main() {
  const uint8_t cie[] = {DW_CFA_def_cfa, 7, 8, DW_CFA_offset | 1, 1};
  const uint8_t nested[] = {DW_CFA_remember_state,
                            DW_CFA_def_cfa_offset,
                            16,
                            DW_CFA_offset | 1,
                            2,
                            DW_CFA_remember_state,
                            DW_CFA_def_cfa_offset,
                            24,
                            DW_CFA_offset | 1,
                            3,
                            DW_CFA_advance_loc | 1,
                            DW_CFA_restore_state,
                            DW_CFA_advance_loc | 1,
                            DW_CFA_restore_state,
                            DW_CFA_advance_loc | 1};
  for (unsigned pc = 0; pc != 5; ++pc) {
    auto s = parse(cie, sizeof(cie), nested, sizeof(nested), pc);
    int expected = pc == 0 || pc >= 3 ? 8 : pc == 1 ? 24 : 16;
    assert(s.cfaRegister == 7 && s.cfaRegisterOffset == expected);
    assert(s.getRegisterLocation(1).location == Parser::kRegisterInCFA);
    assert(s.getRegisterLocation(1).value == -expected);
  }
  // A CIE remember survives into the FDE. A later register restore still uses
  // the completed CIE rule, not the pre-remember state recovered by restore_state.
  const uint8_t crossCIE[] = {DW_CFA_def_cfa,
                              7,
                              8,
                              DW_CFA_offset | 1,
                              1,
                              DW_CFA_remember_state,
                              DW_CFA_def_cfa_offset,
                              16,
                              DW_CFA_offset | 1,
                              2};
  const uint8_t crossFDE[] = {DW_CFA_restore_state, DW_CFA_restore | 1};
  auto cross = parse(crossCIE, sizeof(crossCIE), crossFDE, sizeof(crossFDE), 1);
  assert(cross.cfaRegisterOffset == 8);
  assert(cross.getRegisterLocation(1).value == -16);
  const uint8_t underflow[] = {DW_CFA_restore_state};
  parse(cie, sizeof(cie), underflow, sizeof(underflow), 1, false);
  const uint8_t invalid[] = {DW_CFA_remember_state, 0x17, DW_CFA_restore_state};
  parse(cie, sizeof(cie), invalid, sizeof(invalid), 1, false);
  // Validation must also happen in a discarded scope. 16383 exceeds every
  // register model selected by this test.
  const uint8_t invalidRegister[] = {DW_CFA_remember_state,
                                     DW_CFA_register, 0xff, 0x7f, 1,
                                     DW_CFA_restore_state};
  parse(cie, sizeof(cie), invalidRegister, sizeof(invalidRegister), 1, false);
  const uint8_t cutoff[] = {DW_CFA_remember_state, DW_CFA_def_cfa_offset,
                            32, DW_CFA_advance_loc | 1, 0x17};
  auto cut = parse(cie, sizeof(cie), cutoff, sizeof(cutoff), 1);
  assert(cut.cfaRegisterOffset == 32);
  parse(cie, sizeof(cie), cutoff, sizeof(cutoff), 2, false);
  const uint8_t rules[] = {DW_CFA_remember_state, DW_CFA_GNU_args_size, 16,
                           DW_CFA_undefined, 1, DW_CFA_restore_state,
                           DW_CFA_val_offset_sf, 2, 0x7e,
                           DW_CFA_register, 3, 4,
                           DW_CFA_def_cfa_offset_sf, 0x7d};
  auto rule = parse(cie, sizeof(cie), rules, sizeof(rules), 1);
  assert(rule.spExtraArgSize == 0 && rule.cfaRegisterOffset == 24);
  assert(rule.getRegisterLocation(1).value == -8);
  assert(rule.getRegisterLocation(1).location == Parser::kRegisterInCFA);
  assert(rule.getRegisterLocation(2).value == 16);
  assert(rule.getRegisterLocation(2).location == Parser::kRegisterOffsetFromCFA);
  assert(rule.getRegisterLocation(3).location == Parser::kRegisterInRegister);
  assert(rule.getRegisterLocation(3).value == 4);
  // Expression payload bytes that resemble stack opcodes must not be scanned
  // as CFI. Their rule is discarded; a subsequent CFA expression is retained.
  const uint8_t expression[] = {DW_CFA_remember_state,
                                DW_CFA_expression,
                                1,
                                2,
                                DW_CFA_restore_state,
                                DW_CFA_remember_state,
                                DW_CFA_restore_state,
                                DW_CFA_def_cfa_expression,
                                1,
                                0x30};
  auto exp = parse(cie, sizeof(cie), expression, sizeof(expression), 1);
  assert(exp.cfaRegister == 0);
  assert(exp.cfaExpression == reinterpret_cast<intptr_t>(expression + 8));
  assert(exp.getRegisterLocation(1).value == -8);
  // Deep nesting and repeated scopes have no storage quota or recursive frame.
  uint8_t deep[4097];
  for (unsigned i = 0; i != 2048; ++i)
    deep[i] = DW_CFA_remember_state;
  for (unsigned i = 2048; i != 4096; ++i)
    deep[i] = DW_CFA_restore_state;
  deep[4096] = DW_CFA_nop;
  auto d = parse(cie, sizeof(cie), deep, sizeof(deep), 1);
  assert(d.cfaRegisterOffset == 8 && d.getRegisterLocation(1).value == -8);
  for (unsigned i = 0; i != 4096; i += 2) {
    deep[i] = DW_CFA_remember_state;
    deep[i + 1] = DW_CFA_restore_state;
  }
  d = parse(cie, sizeof(cie), deep, sizeof(deep), 1);
  assert(d.cfaRegisterOffset == 8);
  puts("remember-state-replay: PASS");
  return 0;
}
