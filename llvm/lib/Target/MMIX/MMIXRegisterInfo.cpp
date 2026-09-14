//===-- MMIXRegisterInfo.cpp - MMIX register information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MMIXRegisterInfo.h"
#include "MCTargetDesc/MMIXMCTargetDesc.h"
#include "MMIXCallingConv.h"
#include "MMIXFrameLowering.h"
#include "MMIXInstrInfo.h"
#include "MMIXMachineFunctionInfo.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "MMIXGenRegisterInfo.inc"

MMIXRegisterInfo::MMIXRegisterInfo() : MMIXGenRegisterInfo(MMIX::RJ) {}

const MCPhysReg *
MMIXRegisterInfo::getCalleeSavedRegs(const MachineFunction *) const {
  return CSR_MMIX_SaveList;
}

const uint32_t *
MMIXRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID CC) const {
  if (!isSupportedMMIXCallingConv(CC))
    return nullptr;
  return MF.getTarget().getTargetTriple().isOSLinux() ? CSR_MMIX_Linux_RegMask
                                                   : CSR_MMIX_RegMask;
}

BitVector MMIXRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs(), true);

  for (MCPhysReg Reg : MMIX::GPR64CodeGenRegClass)
    Reserved.reset(Reg);

  // A non-leaf function keeps its incoming rJ in the highest allocatable
  // local register. Calls push through r31, which is the architectural hole.
  if (MF.getFrameInfo().hasCalls())
    Reserved.set(MMIX::R30);

  if (static_cast<const MMIXFrameLowering *>(getFrameLowering(MF))
          ->hasBasePointer(MF))
    Reserved.set(MMIX::R29);

  return Reserved;
}

const TargetRegisterClass *
MMIXRegisterInfo::getPointerRegClass(unsigned Kind) const {
  assert(Kind == 0 && "MMIX has only one pointer register kind");
  return &MMIX::GPR64CodeGenRegClass;
}

static unsigned getRegisterOffsetOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    report_fatal_error("MMIX cannot use a register frame offset for this "
                       "instruction");
#define MAP_IMMEDIATE_TO_REGISTER(Immediate, Register)                         \
  case MMIX::Immediate:                                                        \
    return MMIX::Register
    MAP_IMMEDIATE_TO_REGISTER(ADDUI, ADDU);
    MAP_IMMEDIATE_TO_REGISTER(LDBI, LDB);
    MAP_IMMEDIATE_TO_REGISTER(LDBUI, LDBU);
    MAP_IMMEDIATE_TO_REGISTER(LDWI, LDW);
    MAP_IMMEDIATE_TO_REGISTER(LDWUI, LDWU);
    MAP_IMMEDIATE_TO_REGISTER(LDTI, LDT);
    MAP_IMMEDIATE_TO_REGISTER(LDTUI, LDTU);
    MAP_IMMEDIATE_TO_REGISTER(LDOI, LDO);
    MAP_IMMEDIATE_TO_REGISTER(LDOUI, LDOU);
    MAP_IMMEDIATE_TO_REGISTER(LDHTI, LDHT);
    MAP_IMMEDIATE_TO_REGISTER(LDSFI, LDSF);
    MAP_IMMEDIATE_TO_REGISTER(STBI, STB);
    MAP_IMMEDIATE_TO_REGISTER(STBUI, STBU);
    MAP_IMMEDIATE_TO_REGISTER(STWI, STW);
    MAP_IMMEDIATE_TO_REGISTER(STWUI, STWU);
    MAP_IMMEDIATE_TO_REGISTER(STTI, STT);
    MAP_IMMEDIATE_TO_REGISTER(STTUI, STTU);
    MAP_IMMEDIATE_TO_REGISTER(STOI, STO);
    MAP_IMMEDIATE_TO_REGISTER(STOUI, STOU);
    MAP_IMMEDIATE_TO_REGISTER(STHTI, STHT);
    MAP_IMMEDIATE_TO_REGISTER(STSFI, STSF);
#undef MAP_IMMEDIATE_TO_REGISTER
  }
}

bool MMIXRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                           int SPAdj, unsigned FIOperandNum,
                                           RegScavenger *) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  const auto *TFI = MF.getSubtarget().getFrameLowering();
  if (SPAdj && !TFI->hasFP(MF))
    report_fatal_error("MMIX cannot adjust an SP-relative frame index");
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  Register FrameReg;
  StackOffset Offset = TFI->getFrameIndexReference(MF, FrameIndex, FrameReg);

  if (!MI.getOperand(FIOperandNum + 1).isImm())
    report_fatal_error("MMIX frame index must have an immediate displacement");
  Offset += StackOffset::getFixed(MI.getOperand(FIOperandNum + 1).getImm());

  bool IsPreFramePointerStore = false;
  if (MI.mayStore() && TFI->hasFP(MF)) {
    for (const CalleeSavedInfo &CSI : MF.getFrameInfo().getCalleeSavedInfo()) {
      if (CSI.getReg() == MMIX::R253 && CSI.getFrameIdx() == FrameIndex) {
        IsPreFramePointerStore = true;
        break;
      }
    }
    const auto *MMFI = MF.getInfo<MMIXMachineFunctionInfo>();
    IsPreFramePointerStore |=
        MMFI->hasUnwindReturnAddressFrameIndex() &&
        MMFI->getUnwindReturnAddressFrameIndex() == FrameIndex;
  }

  // Prologue spills emitted before the new frame pointer is established must
  // use the adjusted SP.
  if (IsPreFramePointerStore) {
    FrameReg = MMIX::R254;
    Offset =
        StackOffset::getFixed(MF.getFrameInfo().getObjectOffset(FrameIndex) +
                              MF.getFrameInfo().getStackSize() +
                              MF.getFrameInfo().getOffsetAdjustment() +
                              MI.getOperand(FIOperandNum + 1).getImm());
  }

  int64_t FixedOffset = Offset.getFixed();
  MI.getOperand(FIOperandNum).ChangeToRegister(FrameReg, /*isDef=*/false);

  if (MI.isDebugValue()) {
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(FixedOffset);
    return false;
  }

  if (isUInt<8>(FixedOffset)) {
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(FixedOffset);
    return false;
  }

  const auto &TII =
      *static_cast<const MMIXInstrInfo *>(MF.getSubtarget().getInstrInfo());
  TII.loadImmediate(*MI.getParent(), II, MI.getDebugLoc(), MMIX::R255,
                    uint64_t(FixedOffset));
  MI.setDesc(TII.get(getRegisterOffsetOpcode(MI.getOpcode())));
  MI.getOperand(FIOperandNum + 1)
      .ChangeToRegister(MMIX::R255, /*isDef=*/false, /*isImp=*/false,
                        /*isKill=*/true);
  return false;
}

Register MMIXRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return getFrameLowering(MF)->hasFP(MF) ? MMIX::R253 : MMIX::R254;
}
