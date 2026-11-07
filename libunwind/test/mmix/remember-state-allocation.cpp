//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#define NDEBUG
#include <stdlib.h>

#include "AddressSpace.hpp"
#include "DwarfParser.hpp"

#undef NDEBUG
#include <assert.h>

// Link wrappers leave the real parser's heap-selection macros intact.
extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
static bool active;
static unsigned attempts, released, live, failAt;
static void *owned[4];
extern "C" void *__wrap_malloc(size_t size) {
  if (!active)
    return __real_malloc(size);
  if (++attempts == failAt)
    return nullptr;
  assert(live < 4);
  void *p = __real_malloc(size);
  assert(p);
  owned[live++] = p;
  return p;
}
extern "C" void __wrap_free(void *p) {
  if (active) {
    // Remembered states must be released once, in stack order, on every exit.
    assert(live && owned[live - 1] == p);
    --live;
    ++released;
  }
  __real_free(p);
}

using Parser = libunwind::CFI_Parser<libunwind::LocalAddressSpace>;
using Registers = libunwind::Registers_x86_64;

enum class Tail { Normal, ReallocateAndReject, Underflow };

static void check(unsigned failure, uintptr_t pc, bool success,
                  unsigned expectedAttempts, unsigned expectedReleases,
                  int expectedOffset, Tail tail = Tail::Normal) {
  // A CIE-owned remembered state survives into the FDE. The FDE allocates
  // another state, then restores both. Advancing to PC+1 can stop before either
  // restore, which requires destructor cleanup rather than restore cleanup.
  const uint8_t cieCode[] = {DW_CFA_def_cfa, 7, 8, DW_CFA_remember_state,
                             DW_CFA_def_cfa_offset, 16};
  const uint8_t fdeCode[] = {
      DW_CFA_remember_state, DW_CFA_def_cfa_offset, 24,
      DW_CFA_advance_loc | 1, DW_CFA_restore_state,
      static_cast<uint8_t>(tail == Tail::ReallocateAndReject
                               ? DW_CFA_remember_state
                               : DW_CFA_restore_state),
      static_cast<uint8_t>(tail == Tail::Underflow ? DW_CFA_restore_state
                                                 : 0x17)};
  Parser::CIE_Info cie = {};
  cie.cieStart = cie.cieInstructions = (uintptr_t)cieCode;
  cie.cieLength = sizeof(cieCode);
  cie.codeAlignFactor = 1;
  cie.dataAlignFactor = -8;
  Parser::FDE_Info fde = {};
  fde.fdeStart = fde.fdeInstructions = (uintptr_t)fdeCode;
  // Reallocation leaves the CIE state live when the unknown opcode rejects.
  // Underflow restores both entries before attempting an extra restore.
  fde.fdeLength = sizeof(fdeCode) - (tail == Tail::Normal);
  fde.pcStart = 0x1000;
  fde.pcEnd = 0x1002;
  Parser::PrologInfo state;
  libunwind::LocalAddressSpace space;
  attempts = released = live = 0;
  failAt = failure;
  active = true;
  bool result = Parser::parseFDEInstructions<Registers>(
      space, fde, cie, pc, Registers::getArch(), &state);
  active = false;
  assert(result == success);
  assert(attempts == expectedAttempts && released == expectedReleases);
  assert(live == 0);
  if (success)
    assert(state.cfaRegister == 7 && state.cfaRegisterOffset == expectedOffset);
}

int main() {
  for (unsigned retry = 0; retry != 2; ++retry) {
    check(1, 0x1002, false, 1, 0, 0); // CIE allocation fails.
    check(2, 0x1002, false, 2, 1, 0); // FDE failure releases CIE state.
    check(0, 0x1000, true, 1, 1, 16); // No FDE instructions needed.
    check(0, 0x1001, true, 2, 2, 24); // Target PC precedes restore opcodes.
    check(0, 0x1002, true, 2, 2, 8);  // Both restores execute normally.
    // Mixed explicit restoration and destructor cleanup must release each
    // entry exactly once, including after a later allocation/parser failure.
    check(3, 0x1002, false, 3, 2, 0, Tail::ReallocateAndReject);
    check(0, 0x1002, false, 3, 3, 0, Tail::ReallocateAndReject);
    check(0, 0x1002, false, 2, 2, 0, Tail::Underflow);
  }
}
