//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#define NDEBUG
#include <stdlib.h>

#include "AddressSpace.hpp"
#include "DwarfParser.hpp"
#include "DwarfInstructions.hpp"

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
#if defined(_LIBUNWIND_TARGET_MMIX)
using Registers = libunwind::Registers_mmix;
static constexpr uint8_t CFARegister = UNW_MMIX_R254;
static constexpr uint8_t ReturnRegister = UNW_MMIX_RJ;
#else
using Registers = libunwind::Registers_x86_64;
static constexpr uint8_t CFARegister = 7;
static constexpr uint8_t ReturnRegister = 16;
#endif

enum class Tail { Normal, ReallocateAndReject, Underflow };

static void check(unsigned failure, uintptr_t pc, bool success,
                  unsigned expectedAttempts, unsigned expectedReleases,
                  int expectedOffset, Tail tail = Tail::Normal) {
  // A CIE-owned remembered state survives into the FDE. The FDE allocates
  // another state, then restores both. Advancing to PC+1 can stop before either
  // restore, which requires destructor cleanup rather than restore cleanup.
  const uint8_t cieCode[] = {DW_CFA_def_cfa, CFARegister, 8, DW_CFA_remember_state,
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
    assert(state.cfaRegister == CFARegister &&
           state.cfaRegisterOffset == expectedOffset);
}

static unsigned commits;
static int beforeCommit(libunwind::LocalAddressSpace &, const Registers &,
                        Registers &, bool) {
  ++commits;
  return UNW_STEP_SUCCESS;
}

static void checkStep(unsigned failure, bool stage2) {
  // Real encoded CIE/FDE records exercise decode and the caller's error path,
  // rather than supplying already decoded parser descriptors.
  uint8_t bytes[128] = {};
  size_t end = 0;
  auto word = [&](uint32_t value) {
    memcpy(bytes + end, &value, sizeof(value));
    end += sizeof(value);
  };
  auto address = [&](uintptr_t value) {
    memcpy(bytes + end, &value, sizeof(value));
    end += sizeof(value);
  };
  word(0); // CIE length, filled below.
  word(0); // .eh_frame CIE identifier.
  bytes[end++] = 1; // Version.
  bytes[end++] = 0; // Empty augmentation.
  bytes[end++] = 1; // Code alignment.
  bytes[end++] = 0x78; // SLEB128 data alignment: -8.
  bytes[end++] = ReturnRegister;
  const uint8_t cie[] = {DW_CFA_def_cfa, CFARegister, 8,
                         DW_CFA_offset | ReturnRegister, 1,
                         DW_CFA_remember_state};
  memcpy(bytes + end, cie, sizeof(cie));
  end += sizeof(cie);
  uint32_t length = static_cast<uint32_t>(end - 4);
  memcpy(bytes, &length, sizeof(length));
  size_t fde = end;
  word(0);
  word(static_cast<uint32_t>(end)); // Backward offset to CIE start.
  address(0x1000);
  address(8);
  const uint8_t code[] = {DW_CFA_remember_state, DW_CFA_restore_state,
                          DW_CFA_restore_state};
  memcpy(bytes + end, code, sizeof(code));
  end += sizeof(code);
  length = static_cast<uint32_t>(end - fde - 4);
  memcpy(bytes + fde, &length, sizeof(length));

  uintptr_t stack[] = {0x1234};
  Registers registers;
  registers.setSP(reinterpret_cast<uintptr_t>(stack));
  registers.setIP(0x1001);
  unsigned char original[sizeof(registers)];
  memcpy(original, &registers, sizeof(registers));
  bool signalFrame = true;
  libunwind::LocalAddressSpace space;
  attempts = released = live = commits = 0;
  failAt = failure;
  active = true;
  int result = libunwind::DwarfInstructions<libunwind::LocalAddressSpace,
                                           Registers>::stepWithDwarf(
      space, 0x1001, reinterpret_cast<uintptr_t>(bytes + fde), registers,
      signalFrame, stage2, beforeCommit);
  active = false;
  assert(live == 0);
  if (failure) {
    assert(result == UNW_EBADFRAME && commits == 0 && signalFrame);
    assert(memcmp(original, &registers, sizeof(registers)) == 0);
    assert(attempts == failure && released == failure - 1);
  } else {
    assert(result == UNW_STEP_SUCCESS && commits == 1 && !signalFrame);
    assert(registers.getIP() == 0x1234);
    assert(registers.getSP() == reinterpret_cast<uintptr_t>(stack + 1));
    assert(attempts == 2 && released == 2);
  }
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
    for (unsigned stage2 = 0; stage2 != 2; ++stage2) {
      checkStep(1, stage2);
      checkStep(2, stage2);
      checkStep(0, stage2);
    }
  }
  return 0;
}
