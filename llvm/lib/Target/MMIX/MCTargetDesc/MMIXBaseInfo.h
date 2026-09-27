//===-- MMIXBaseInfo.h - MMIX instruction metadata ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MMIX_MCTARGETDESC_MMIXBASEINFO_H
#define LLVM_LIB_TARGET_MMIX_MCTARGETDESC_MMIXBASEINFO_H

#include "llvm/MC/MCInstrDesc.h"
#include <cstdint>

namespace llvm {
namespace MMIXII {

enum AsmVariant : unsigned {
  CanonicalAsmVariant = 0,
  MMIXALAsmVariant = 1,
};

enum OperandType : unsigned {
  OPERAND_UIMM8 = MCOI::OPERAND_FIRST_TARGET,
  OPERAND_UIMM16,
  OPERAND_ROUNDING_MODE,
  OPERAND_RESUME_MODE,
  OPERAND_SYNC_MODE,
  OPERAND_REG_OR_IMM8,
};

enum MMIXALSelectionKind : unsigned {
  MMIXALSelectionUnclassified,
  MMIXALSelectionExact,
  MMIXALSelectionRegister,
  MMIXALSelectionImmediate,
  MMIXALSelectionForward,
  MMIXALSelectionBackward,
};

enum : uint64_t {
  OpcodeMask = 0xff,
  PCRelativeBackward = uint64_t(1) << 8,
  PCRelativeWidthShift = 9,
  PCRelativeWidthMask = uint64_t(0x1f) << PCRelativeWidthShift,
  PutSpecialRegister = uint64_t(1) << 14,
  PutSpecialRegisterImmediate = uint64_t(1) << 15,
  MMIXALSelectionShift = 16,
  MMIXALSelectionMask = uint64_t(0x7) << MMIXALSelectionShift,
};

enum MachineOperandFlags {
  MO_None,
  MO_ABS_LO,
  MO_ABS_ML,
  MO_ABS_MH,
  MO_ABS_HI,
  MO_TPREL_LO,
  MO_TPREL_ML,
  MO_TPREL_MH,
  MO_TPREL_HI,
};

enum MCInstFlags {
  DirectionNeutralCall = 1 << 0,
};

enum Specifier : uint16_t {
  S_None,
  S_GETA,
  S_TPREL_LO,
  S_TPREL_ML,
  S_TPREL_MH,
  S_TPREL_HI,
};

inline bool isTPRELSpecifier(unsigned Specifier) {
  return Specifier >= S_TPREL_LO && Specifier <= S_TPREL_HI;
}

// MC relaxation appends this operand and emits the corresponding SWYM slots.
constexpr unsigned GETARelocationReservedSlots = 3;

inline unsigned getPCRelativeWidth(uint64_t TSFlags) {
  return (TSFlags & PCRelativeWidthMask) >> PCRelativeWidthShift;
}

inline MMIXALSelectionKind getMMIXALSelection(uint64_t TSFlags) {
  return static_cast<MMIXALSelectionKind>((TSFlags & MMIXALSelectionMask) >>
                                          MMIXALSelectionShift);
}

} // namespace MMIXII
} // namespace llvm

#endif
