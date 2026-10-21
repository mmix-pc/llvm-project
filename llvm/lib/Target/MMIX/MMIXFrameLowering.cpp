//===-- MMIXFrameLowering.cpp - MMIX frame lowering ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MMIXFrameLowering.h"
#include "MCTargetDesc/MMIXBaseInfo.h"
#include "MCTargetDesc/MMIXMCTargetDesc.h"
#include "MMIXInstrInfo.h"
#include "MMIXMachineFunctionInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetMachine.h"

#include <iterator>

using namespace llvm;

static bool needsRuntimeCFI(const MachineFunction &MF) {
  return MF.getFunction().hasUWTable() ||
         (MF.getTarget().getMCAsmInfo().usesCFIForEH() &&
          MF.getFunction().needsUnwindTableEntry());
}

static bool needsCFI(const MachineFunction &MF) {
  return (MF.getFunction().getSubprogram() != nullptr || needsRuntimeCFI(MF)) &&
         MF.getTarget().getMCAsmInfo().getOutputAssemblerDialect() ==
             MMIXII::CanonicalAsmVariant;
}

static void emitCFI(MachineFunction &MF, MachineBasicBlock &MBB,
                    MachineBasicBlock::iterator MBBI,
                    const MCCFIInstruction &Instruction,
                    MachineInstr::MIFlag Flag) {
  unsigned Index = MF.addFrameInst(Instruction);
  BuildMI(MBB, MBBI, DebugLoc(),
          MF.getSubtarget().getInstrInfo()->get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(Index)
      .setMIFlag(Flag);
}

MMIXFrameLowering::MMIXFrameLowering()
    : TargetFrameLowering(StackGrowsDown, Align(8), /*LocalAreaOffset=*/0,
                          Align(8)) {}

bool MMIXFrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  return !MF.getFrameInfo().hasVarSizedObjects();
}

bool MMIXFrameLowering::hasBasePointer(const MachineFunction &MF) const {
  return MF.getFrameInfo().hasVarSizedObjects() &&
         MF.getSubtarget().getRegisterInfo()->hasStackRealignment(MF);
}

StackOffset
MMIXFrameLowering::getFrameIndexReference(const MachineFunction &MF, int FI,
                                          Register &FrameReg) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  int64_t Offset = MFI.getObjectOffset(FI) + MFI.getOffsetAdjustment();

  if (MF.getSubtarget().getRegisterInfo()->hasStackRealignment(MF) &&
      !MFI.isFixedObjectIndex(FI) && !MFI.isCalleeSavedObjectIndex(FI)) {
    // BP keeps the aligned fixed-frame base stable across dynamic allocations.
    // Incoming arguments and saved registers retain entry-SP-relative offsets.
    FrameReg = hasBasePointer(MF) ? MMIX::R29 : MMIX::R254;
    Offset += MFI.getStackSize();
  } else if (hasFP(MF)) {
    FrameReg = MMIX::R253;
  } else {
    FrameReg = MMIX::R254;
    Offset += MFI.getStackSize();
  }

  return StackOffset::getFixed(Offset);
}

void MMIXFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                             BitVector &SavedRegs,
                                             RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);

  // PUSHJ/PUSHGO and POP preserve local registers through the hardware
  // register stack, so they must never receive ordinary memory save slots.
  for (MCPhysReg Reg = MMIX::R0; Reg <= MMIX::R30; ++Reg)
    SavedRegs.reset(Reg);

  if (hasFP(MF))
    SavedRegs.set(MMIX::R253);

  if (MF.getFrameInfo().hasCalls() && needsCFI(MF)) {
    auto *MMFI = MF.getInfo<MMIXMachineFunctionInfo>();
    if (!MMFI->hasUnwindReturnAddressFrameIndex()) {
      MachineFrameInfo &MFI = MF.getFrameInfo();
      int FI;
      if (MF.getSubtarget().getRegisterInfo()->hasStackRealignment(MF)) {
        // Keep rJ below the old FP and any variadic register saves.
        // A shifted local slot cannot be described by a constant CFA offset.
        int64_t Offset = 0;
        for (int I = MFI.getObjectIndexBegin(); I < 0; ++I)
          Offset = std::min(Offset, MFI.getObjectOffset(I));
        FI = MFI.CreateFixedSpillStackObject(8, Offset - 16);
      } else {
        FI = MFI.CreateStackObject(8, Align(8), false);
      }
      MMFI->setUnwindReturnAddressFrameIndex(FI);
    }
  }
}

bool MMIXFrameLowering::assignCalleeSavedSpillSlots(
    MachineFunction &MF, const TargetRegisterInfo *TRI,
    std::vector<CalleeSavedInfo> &CSI) const {
  auto *MMFI = MF.getInfo<MMIXMachineFunctionInfo>();
  if (!TRI->hasStackRealignment(MF) ||
      !MMFI->hasUnwindReturnAddressFrameIndex())
    return false;
  // The fixed rJ slot must not push the traditional old-FP slot downward.
  assert(CSI.size() == 1 && CSI.front().getReg() == MMIX::R253);
  MachineFrameInfo &MFI = MF.getFrameInfo();
  int64_t Offset =
      MFI.getObjectOffset(MMFI->getUnwindReturnAddressFrameIndex()) + 8;
  CSI.front().setFrameIdx(MFI.CreateFixedSpillStackObject(8, Offset));
  return true;
}

void MMIXFrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  for (int FI = 0, End = MFI.getObjectIndexEnd(); FI != End; ++FI) {
    if (!MFI.isDeadObjectIndex(FI) && MFI.getObjectAlign(FI) < Align(4))
      MFI.setObjectAlignment(FI, Align(4));
  }
}

static void validateFrame(const MachineFunction &MF) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  if (MF.getSubtarget().getRegisterInfo()->hasStackRealignment(MF)) {
    StringRef Reason;
    if (MFI.hasOpaqueSPAdjustment())
      Reason = " with opaque stack adjustments";
    else if (MF.getTarget().getMCAsmInfo().getOutputAssemblerDialect() !=
             MMIXII::CanonicalAsmVariant)
      Reason = " for MMIXAL output";
    if (!Reason.empty())
      reportFatalUsageError(Twine("MMIX does not support stack realignment") +
                            Reason + " in function '" + MF.getName() + "'");
  }
  if (MF.shouldSplitStack())
    reportFatalUsageError(
        Twine("MMIX does not support split stacks in function '") +
        MF.getName() + "'");
  if (MF.getFunction().hasFnAttribute("probe-stack"))
    reportFatalUsageError(
        Twine("MMIX does not support stack probing in function '") +
        MF.getName() + "'");
  if (needsRuntimeCFI(MF) && MFI.hasOpaqueSPAdjustment())
    reportFatalUsageError(
        Twine("MMIX cannot unwind opaque stack adjustments in function '") +
        MF.getName() + "'");
}

MMIXTailCallFrameState
MMIXFrameLowering::analyzeTailCallFrame(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  MMIXTailCallFrameState State;
  State.HasDynamicStack = MFI.hasVarSizedObjects();
  State.RequiresStackRealignment =
      MF.getSubtarget().getRegisterInfo()->hasStackRealignment(MF);
  State.CanRestoreFrame =
      !MFI.hasOpaqueSPAdjustment() && !MF.shouldSplitStack() &&
      !MF.getFunction().hasFnAttribute("probe-stack") &&
      MFI.getStackSize() <= uint64_t(INT64_MAX);
  return State;
}

void MMIXFrameLowering::emitPrologue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  assert(&MBB == &MF.front() && "MMIX does not support shrink wrapping");
  validateFrame(MF);

  if (hasBasePointer(MF))
    for (const MachineBasicBlock &Block : MF)
      for (const MachineInstr &MI : Block)
        if (MI.isInlineAsm() &&
            (MI.readsRegister(MMIX::R29, MF.getSubtarget().getRegisterInfo()) ||
             MI.modifiesRegister(MMIX::R29,
                                 MF.getSubtarget().getRegisterInfo())))
          reportFatalUsageError(
              Twine("MMIX inline assembly cannot use r29 in a realigned ") +
              "dynamic frame in function '" + MF.getName() + "'");

  MachineFrameInfo &MFI = MF.getFrameInfo();
  uint64_t StackSize = MFI.getStackSize();
  if (StackSize > uint64_t(INT64_MAX))
    report_fatal_error("MMIX stack frame is too large");

  const auto *TII = MF.getSubtarget().getInstrInfo();
  const auto &MMIXII = *static_cast<const MMIXInstrInfo *>(TII);
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL;
  const auto *TRI = MF.getSubtarget().getRegisterInfo();
  const bool NeedsCFI = needsCFI(MF);

  if (NeedsCFI) {
    emitCFI(MF, MBB, MBBI,
            MCCFIInstruction::cfiDefCfa(
                nullptr, TRI->getDwarfRegNum(MMIX::R254, true), 0),
            MachineInstr::FrameSetup);
    emitCFI(MF, MBB, MBBI,
            MCCFIInstruction::createSameValue(
                nullptr, TRI->getDwarfRegNum(MMIX::RJ, true)),
            MachineInstr::FrameSetup);
  }

  if (MFI.hasCalls())
    BuildMI(MBB, MBBI, DL, TII->get(MMIX::GET), MMIX::R30)
        .addReg(MMIX::RJ)
        .setMIFlag(MachineInstr::FrameSetup);

  MMIXII.adjustReg(MBB, MBBI, DL, MMIX::R254, MMIX::R254, -int64_t(StackSize),
                   MachineInstr::FrameSetup);

  if (NeedsCFI) {
    emitCFI(MF, MBB, MBBI,
            MCCFIInstruction::cfiDefCfaOffset(nullptr, StackSize),
            MachineInstr::FrameSetup);

    auto *MMFI = MF.getInfo<MMIXMachineFunctionInfo>();
    if (MMFI->hasUnwindReturnAddressFrameIndex()) {
      int FrameIndex = MMFI->getUnwindReturnAddressFrameIndex();
      TII->storeRegToStackSlot(MBB, MBBI, MMIX::R30, false, FrameIndex,
                               &MMIX::GPR64RegClass, Register(),
                               MachineInstr::FrameSetup);
      emitCFI(MF, MBB, MBBI,
              MCCFIInstruction::createOffset(
                  nullptr, TRI->getDwarfRegNum(MMIX::RJ, true),
                  MFI.getObjectOffset(FrameIndex)),
              MachineInstr::FrameSetup);
    }
  }

  if (hasFP(MF)) {
    // The generic callee-save spill of the old frame pointer is at the entry
    // of the block. Establish the new frame pointer after that store.
    std::advance(MBBI, MFI.getCalleeSavedInfo().size());
    if (NeedsCFI)
      for (const CalleeSavedInfo &CSI : MFI.getCalleeSavedInfo())
        emitCFI(MF, MBB, MBBI,
                MCCFIInstruction::createOffset(
                    nullptr, TRI->getDwarfRegNum(CSI.getReg(), true),
                    MFI.getObjectOffset(CSI.getFrameIdx())),
                MachineInstr::FrameSetup);
    MMIXII.adjustReg(MBB, MBBI, DL, MMIX::R253, MMIX::R254, int64_t(StackSize),
                     MachineInstr::FrameSetup);
    if (NeedsCFI)
      emitCFI(MF, MBB, MBBI,
              MCCFIInstruction::cfiDefCfa(
                  nullptr, TRI->getDwarfRegNum(MMIX::R253, true), 0),
              MachineInstr::FrameSetup);
  }
  if (TRI->hasStackRealignment(MF)) {
    uint64_t Mask = std::max(MFI.getMaxAlign(), getStackAlign()).value() - 1;
    if (isUInt<8>(Mask)) {
      BuildMI(MBB, MBBI, DL, TII->get(MMIX::ANDNI), MMIX::R254)
          .addReg(MMIX::R254)
          .addImm(Mask)
          .setMIFlag(MachineInstr::FrameSetup);
    } else {
      MMIXII.loadImmediate(MBB, MBBI, DL, MMIX::R255, Mask,
                           MachineInstr::FrameSetup);
      BuildMI(MBB, MBBI, DL, TII->get(MMIX::ANDN), MMIX::R254)
          .addReg(MMIX::R254)
          .addReg(MMIX::R255, RegState::Kill)
          .setMIFlag(MachineInstr::FrameSetup);
    }
    if (hasBasePointer(MF))
      MMIXII.adjustReg(MBB, MBBI, DL, MMIX::R29, MMIX::R254, 0,
                       MachineInstr::FrameSetup);
  }
}

void MMIXFrameLowering::emitCallerStateRestore(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MBBI, const DebugLoc &DL) const {
  uint64_t StackSize = MF.getFrameInfo().getStackSize();
  if (StackSize > uint64_t(INT64_MAX))
    report_fatal_error("MMIX stack frame is too large");

  const auto &MMIXII =
      *static_cast<const MMIXInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const auto *TRI = MF.getSubtarget().getRegisterInfo();
  const bool NeedsCFI = needsCFI(MF);
  ArrayRef<CalleeSavedInfo> CSI = MF.getFrameInfo().getCalleeSavedInfo();
  MachineBasicBlock::iterator FirstRestore = std::prev(MBBI, CSI.size());
  // rJ remains available in the preserved local while the memory frame is freed.
  if (NeedsCFI && MF.getFrameInfo().hasCalls())
    emitCFI(MF, MBB, FirstRestore,
            MCCFIInstruction::createRegister(
                nullptr, TRI->getDwarfRegNum(MMIX::RJ, true),
                TRI->getDwarfRegNum(MMIX::R30, true)),
            MachineInstr::FrameDestroy);
  if (MF.getFrameInfo().hasVarSizedObjects() || TRI->hasStackRealignment(MF)) {
    // Retain the fixed save area until its registers have been restored. An
    // interrupt must not find CFI referring to memory below the current SP.
    MMIXII.adjustReg(MBB, FirstRestore, DL, MMIX::R254, MMIX::R253,
                    -int64_t(StackSize), MachineInstr::FrameDestroy);
    if (NeedsCFI)
      emitCFI(MF, MBB, FirstRestore,
              MCCFIInstruction::cfiDefCfa(
                  nullptr, TRI->getDwarfRegNum(MMIX::R254, true), StackSize),
              MachineInstr::FrameDestroy);
  } else {
    if (NeedsCFI && hasFP(MF))
      emitCFI(MF, MBB, FirstRestore,
              MCCFIInstruction::cfiDefCfa(
                  nullptr, TRI->getDwarfRegNum(MMIX::R254, true), StackSize),
              MachineInstr::FrameDestroy);
  }
  if (NeedsCFI)
    for (const CalleeSavedInfo &CSI : MF.getFrameInfo().getCalleeSavedInfo())
      emitCFI(MF, MBB, MBBI,
              MCCFIInstruction::createSameValue(
                  nullptr, TRI->getDwarfRegNum(CSI.getReg(), true)),
              MachineInstr::FrameDestroy);
  MMIXII.adjustReg(MBB, MBBI, DL, MMIX::R254, MMIX::R254,
                   int64_t(StackSize), MachineInstr::FrameDestroy);
  if (NeedsCFI)
    emitCFI(MF, MBB, MBBI, MCCFIInstruction::cfiDefCfaOffset(nullptr, 0),
            MachineInstr::FrameDestroy);
  if (MF.getFrameInfo().hasCalls())
    BuildMI(MBB, MBBI, DL, MMIXII.get(MMIX::PUT), MMIX::RJ)
        .addReg(MMIX::R30)
        .setMIFlag(MachineInstr::FrameDestroy);
  if (NeedsCFI && MF.getFrameInfo().hasCalls())
    emitCFI(MF, MBB, MBBI,
            MCCFIInstruction::createSameValue(
                nullptr, TRI->getDwarfRegNum(MMIX::RJ, true)),
            MachineInstr::FrameDestroy);
}

static bool isTailTransfer(const MachineInstr &MI) {
  return MI.getOpcode() == MMIX::INDIRECT_TAIL_STATE ||
         MI.getOpcode() == MMIX::DIRECT_TAIL_STATE ||
         MI.getOpcode() == MMIX::MATERIALIZED_DIRECT_TAIL_STATE;
}

void MMIXFrameLowering::emitEpilogue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  DebugLoc DL = MBBI == MBB.end() ? DebugLoc() : MBBI->getDebugLoc();
  if (MBBI != MBB.end() && isTailTransfer(*MBBI)) {
    MMIXTailCallFrameState State = analyzeTailCallFrame(MF);
    if (!State.isEligible())
      reportFatalUsageError(
          Twine("MMIX tail transfer is ineligible: ") +
          MMIXTailCallEligibility(State.getReason()).getReasonText() +
          " in function '" + MF.getName() + "'");
  }
  emitCallerStateRestore(MF, MBB, MBBI, DL);
}

MachineBasicBlock::iterator MMIXFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const {
  if (!hasReservedCallFrame(MF)) {
    int64_t Amount = alignTo(MI->getOperand(0).getImm(), getStackAlign());
    if (MI->getOpcode() == MMIX::ADJCALLSTACKDOWN)
      Amount = -Amount;
    const auto &MMIXII = *static_cast<const MMIXInstrInfo *>(
        MF.getSubtarget().getInstrInfo());
    MMIXII.adjustReg(MBB, MI, MI->getDebugLoc(), MMIX::R254, MMIX::R254,
                     Amount);
  }
  return MBB.erase(MI);
}

bool MMIXFrameLowering::hasFPImpl(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
         MFI.hasVarSizedObjects() || MFI.isFrameAddressTaken() ||
         MF.getSubtarget().getRegisterInfo()->hasStackRealignment(MF);
}

bool MMIXFrameLowering::enableCFIFixup(const MachineFunction &MF) const {
  return needsCFI(MF);
}
