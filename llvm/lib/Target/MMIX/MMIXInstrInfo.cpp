//===-- MMIXInstrInfo.cpp - MMIX instruction information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MMIXInstrInfo.h"
#include "MCTargetDesc/MMIXBaseInfo.h"
#include "MCTargetDesc/MMIXMCTargetDesc.h"
#include "MMIXSubtarget.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "MMIXGenInstrInfo.inc"

MMIXInstrInfo::MMIXInstrInfo(const MMIXSubtarget &STI)
    : MMIXGenInstrInfo(STI, RI, MMIX::ADJCALLSTACKDOWN, MMIX::ADJCALLSTACKUP),
      RI() {}

static int getSingleWydeIndex(uint64_t Value) {
  int Index = -1;
  for (int I = 0; I != 4; ++I) {
    if (((Value >> (I * 16)) & 0xffff) == 0)
      continue;
    if (Index != -1)
      return -1;
    Index = I;
  }
  return Index == -1 ? 0 : Index;
}

static bool isRedundantCallStateOperand(const MachineOperand &MO) {
  if (!MO.isReg() || !MO.isImplicit())
    return false;
  return MO.getReg() == MMIX::RJ || MO.getReg() == MMIX::RL ||
         MO.getReg() == MMIX::RO || MO.getReg() == MMIX::R254 ||
         MO.getReg() == MMIX::RG;
}

static void copyCallStateOperands(MachineInstrBuilder &MIB,
                                  const MachineInstr &MI,
                                  unsigned FirstOperand) {
  for (unsigned I = FirstOperand; I != MI.getNumOperands(); ++I)
    if (!isRedundantCallStateOperand(MI.getOperand(I)))
      MIB.add(MI.getOperand(I));
}

void MMIXInstrInfo::loadImmediate(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator MBBI,
                                  const DebugLoc &DL, Register DstReg,
                                  uint64_t Value,
                                  MachineInstr::MIFlag Flags) const {
  static constexpr unsigned SetOpcodes[] = {MMIX::SETL, MMIX::SETML,
                                            MMIX::SETMH, MMIX::SETH};
  static constexpr unsigned IncOpcodes[] = {MMIX::INCL, MMIX::INCML,
                                            MMIX::INCMH, MMIX::INCH};

  // Handle compact signed, shifted-wyde, and complemented forms before the
  // generic SET-plus-INC sequence.
  int64_t SignedValue = static_cast<int64_t>(Value);
  if (SignedValue < 0 && SignedValue >= -255) {
    BuildMI(MBB, MBBI, DL, get(MMIX::NEGUI), DstReg)
        .addImm(0)
        .addImm(-SignedValue)
        .setMIFlag(Flags);
    return;
  }

  auto EmitSingleWyde = [&](uint64_t WydeValue) {
    int Index = getSingleWydeIndex(WydeValue);
    assert(Index >= 0 && "value is not a single wyde");
    BuildMI(MBB, MBBI, DL, get(SetOpcodes[Index]), DstReg)
        .addImm((WydeValue >> (Index * 16)) & 0xffff)
        .setMIFlag(Flags);
  };

  if (getSingleWydeIndex(Value) >= 0) {
    EmitSingleWyde(Value);
    return;
  }

  uint64_t Magnitude = 0 - Value;
  if (SignedValue < 0 && getSingleWydeIndex(Magnitude) >= 0) {
    EmitSingleWyde(Magnitude);
    BuildMI(MBB, MBBI, DL, get(MMIX::NEGU), DstReg)
        .addImm(0)
        .addReg(DstReg)
        .setMIFlag(Flags);
    return;
  }

  uint64_t Complement = ~Value;
  if (getSingleWydeIndex(Complement) >= 0) {
    EmitSingleWyde(Complement);
    BuildMI(MBB, MBBI, DL, get(MMIX::NORI), DstReg)
        .addReg(DstReg)
        .addImm(0)
        .setMIFlag(Flags);
    return;
  }

  bool First = true;
  for (int I = 0; I != 4; ++I) {
    uint64_t Part = (Value >> (I * 16)) & 0xffff;
    if (!Part)
      continue;
    BuildMI(MBB, MBBI, DL, get(First ? SetOpcodes[I] : IncOpcodes[I]), DstReg)
        .addImm(Part)
        .setMIFlag(Flags);
    First = false;
  }
}

bool MMIXInstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  if (MI.getOpcode() == MMIX::LOAD_TLS_ADDR) {
    Register Dst = MI.getOperand(0).getReg();
    const MachineOperand &Symbol = MI.getOperand(1);
    static constexpr unsigned Opcodes[] = {MMIX::SETL, MMIX::INCML,
                                          MMIX::INCMH, MMIX::INCH};
    static constexpr unsigned Flags[] = {MMIXII::MO_TPREL_LO, MMIXII::MO_TPREL_ML,
                                         MMIXII::MO_TPREL_MH, MMIXII::MO_TPREL_HI};
    for (unsigned I = 0; I != 4; ++I) {
      auto MIB = BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), get(Opcodes[I]),
                         Dst)
                     .addGlobalAddress(Symbol.getGlobal(), Symbol.getOffset(),
                                       Flags[I]);
      // INC reads the preceding value even though MC prints only X and YZ.
      if (I)
        MIB.addReg(Dst, RegState::Implicit);
    }
    BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), get(MMIX::ADDU), Dst)
        .addReg(MMIX::R230)
        .addReg(Dst, RegState::Kill);
    MI.eraseFromParent();
    return true;
  }

  if (MI.getDesc().TSFlags & MMIXII::PutSpecialRegister) {
    Register SpecialReg;
    for (const MachineOperand &MO : MI.operands())
      if (MO.isReg() && MO.isImplicit() && MO.isDef()) {
        SpecialReg = MO.getReg();
        break;
      }
    assert(SpecialReg && "special-register PUT pseudo has no implicit def");
    unsigned Opcode = MI.getDesc().TSFlags & MMIXII::PutSpecialRegisterImmediate
                          ? MMIX::PUTI
                          : MMIX::PUT;
    BuildMI(*MI.getParent(), MI.getIterator(), MI.getDebugLoc(), get(Opcode),
            SpecialReg)
        .add(MI.getOperand(0));
    MI.eraseFromParent();
    return true;
  }

  if (MI.getOpcode() == MMIX::ATOMIC_CMP_SWAP) {
    MachineBasicBlock &MBB = *MI.getParent();
    MachineBasicBlock::iterator MBBI = MI.getIterator();
    const DebugLoc &DL = MI.getDebugLoc();
    Register Old = MI.getOperand(0).getReg();
    Register Ptr = MI.getOperand(1).getReg();
    Register Expected = MI.getOperand(2).getReg();
    Register New = MI.getOperand(3).getReg();

    auto WasKilled = [&](Register Reg) {
      for (unsigned I = 1; I != 4; ++I)
        if (MI.getOperand(I).getReg() == Reg && MI.getOperand(I).isKill())
          return true;
      return false;
    };
    bool KillExpected =
        WasKilled(Expected) && Expected != New && Expected != Ptr;
    bool KillNew = WasKilled(New) && New != Ptr;
    bool KillPtr = WasKilled(Ptr);
    BuildMI(MBB, MBBI, DL, get(MMIX::PUT), MMIX::RP)
        .addReg(Expected, getKillRegState(KillExpected));
    copyPhysReg(MBB, MBBI, DL, MMIX::R255, New, KillNew);
    BuildMI(MBB, MBBI, DL, get(MMIX::CSWAPI), MMIX::R255)
        .addReg(MMIX::R255, RegState::Kill)
        .addReg(Ptr, getKillRegState(KillPtr))
        .addImm(0)
        .cloneMemRefs(MI);
    BuildMI(MBB, MBBI, DL, get(MMIX::GET), Old).addReg(MMIX::RP);
    MI.eraseFromParent();
    return true;
  }

  if (MI.getOpcode() == MMIX::TRAP_STATE) {
    MachineBasicBlock &MBB = *MI.getParent();
    MachineBasicBlock::iterator MBBI = MI.getIterator();
    const DebugLoc &DL = MI.getDebugLoc();
    Register Result = MI.getOperand(0).getReg();
    Register Argument = MI.getOperand(1).getReg();

    copyPhysReg(MBB, MBBI, DL, MMIX::R255, Argument, MI.getOperand(1).isKill());
    BuildMI(MBB, MBBI, DL, get(MMIX::TRAP))
        .addImm(0)
        .add(MI.getOperand(2))
        .add(MI.getOperand(3));
    if (!MI.getOperand(0).isDead())
      copyPhysReg(MBB, MBBI, DL, Result, MMIX::R255, true);
    MI.eraseFromParent();
    return true;
  }

  if (MI.getOpcode() == MMIX::CALL_STATE) {
    MachineBasicBlock &MBB = *MI.getParent();
    MachineInstrBuilder MIB;
    if (MI.getOperand(0).isReg()) {
      MIB = BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(),
                    get(MMIX::PseudoPUSHGO))
                .addReg(MMIX::R31)
                .add(MI.getOperand(0))
                .addImm(0);
    } else {
      MIB = BuildMI(MBB, MI.getIterator(), MI.getDebugLoc(),
                    get(MMIX::PseudoPUSHJ))
                .addReg(MMIX::R31)
                .add(MI.getOperand(0));
    }
    copyCallStateOperands(MIB, MI, 1);
    MI.eraseFromParent();
    return true;
  }

  if (MI.getOpcode() == MMIX::DIRECT_CALL_STATE) {
    MachineInstrBuilder MIB =
        BuildMI(*MI.getParent(), MI.getIterator(), MI.getDebugLoc(),
                get(MMIX::PseudoDirectCall))
            .addReg(MMIX::R31)
            .add(MI.getOperand(0))
            .add(MI.getOperand(1));
    copyCallStateOperands(MIB, MI, 2);
    MI.eraseFromParent();
    return true;
  }

  if (MI.getOpcode() == MMIX::DIRECT_TAIL_STATE ||
      MI.getOpcode() == MMIX::MATERIALIZED_DIRECT_TAIL_STATE ||
      MI.getOpcode() == MMIX::INDIRECT_TAIL_STATE) {
    MachineInstrBuilder MIB;
    unsigned FirstStateOperand;
    if (MI.getOpcode() == MMIX::DIRECT_TAIL_STATE) {
      MIB = BuildMI(*MI.getParent(), MI.getIterator(), MI.getDebugLoc(),
                    get(MMIX::PseudoDirectTail))
                .add(MI.getOperand(0));
      FirstStateOperand = 1;
    } else if (MI.getOpcode() == MMIX::MATERIALIZED_DIRECT_TAIL_STATE) {
      MIB = BuildMI(*MI.getParent(), MI.getIterator(), MI.getDebugLoc(),
                    get(MMIX::PseudoMaterializedDirectTail))
                .add(MI.getOperand(0))
                .add(MI.getOperand(1));
      FirstStateOperand = 2;
    } else {
      MIB = BuildMI(*MI.getParent(), MI.getIterator(), MI.getDebugLoc(),
                    get(MMIX::PseudoIndirectTail))
                .add(MI.getOperand(0));
      FirstStateOperand = 1;
    }
    copyCallStateOperands(MIB, MI, FirstStateOperand);
    MI.eraseFromParent();
    return true;
  }

  if (MI.getOpcode() == MMIX::SET_RD_ZERO) {
    BuildMI(*MI.getParent(), MI.getIterator(), MI.getDebugLoc(),
            get(MMIX::PUTI))
        .addDef(MMIX::RD)
        .addImm(0);
    MI.eraseFromParent();
    return true;
  }

  if (MI.getOpcode() != MMIX::LOAD_IMM64)
    return false;

  loadImmediate(*MI.getParent(), MI.getIterator(), MI.getDebugLoc(),
                MI.getOperand(0).getReg(), uint64_t(MI.getOperand(1).getImm()));
  MI.eraseFromParent();
  return true;
}

bool MMIXInstrInfo::verifyInstruction(const MachineInstr &MI,
                                      StringRef &ErrInfo) const {
  auto IsDirectTarget = [](const MachineOperand &MO) {
    return MO.isGlobal() || MO.isSymbol() || MO.isMCSymbol();
  };

  unsigned ExplicitOperands;
  switch (MI.getOpcode()) {
  case MMIX::INDIRECT_TAIL_STATE:
    ExplicitOperands = 1;
    if (MI.getNumOperands() < ExplicitOperands || !MI.getOperand(0).isReg()) {
      ErrInfo = "MMIX indirect tail transfer requires a register callee";
      return false;
    }
    break;
  case MMIX::DIRECT_TAIL_STATE:
    ExplicitOperands = 1;
    if (MI.getNumOperands() < ExplicitOperands ||
        !IsDirectTarget(MI.getOperand(0))) {
      ErrInfo = "MMIX direct tail transfer requires a symbolic callee";
      return false;
    }
    break;
  case MMIX::MATERIALIZED_DIRECT_TAIL_STATE:
    ExplicitOperands = 2;
    if (MI.getNumOperands() < ExplicitOperands ||
        !IsDirectTarget(MI.getOperand(0)) || !MI.getOperand(1).isReg()) {
      ErrInfo = "MMIX materialized direct tail transfer requires a symbol and "
                "scratch register";
      return false;
    }
    break;
  default:
    return true;
  }

  if (MI.getNumOperands() <= ExplicitOperands ||
      !MI.getOperand(ExplicitOperands).isRegMask()) {
    ErrInfo =
        "MMIX tail transfer requires a register mask after its callee operands";
    return false;
  }
  for (unsigned I = ExplicitOperands + 1; I != MI.getNumOperands(); ++I) {
    const MachineOperand &MO = MI.getOperand(I);
    if (!MO.isReg() || MO.isDef()) {
      ErrInfo = "MMIX tail transfer accepts only argument register uses after "
                "its register mask";
      return false;
    }
  }

  const MachineFunction &MF = *MI.getMF();
  MMIXTailCallFrameState FrameState =
      MF.getSubtarget<MMIXSubtarget>().getFrameLowering()->analyzeTailCallFrame(
          MF);
  if (!FrameState.isEligible()) {
    ErrInfo = MMIXTailCallEligibility(FrameState.getReason()).getReasonText();
    return false;
  }
  return true;
}

ArrayRef<std::pair<unsigned, const char *>>
MMIXInstrInfo::getSerializableDirectMachineOperandTargetFlags() const {
  static const std::pair<unsigned, const char *> TargetFlags[] = {
      {MMIXII::MO_ABS_LO, "mmix-abs-lo"},
      {MMIXII::MO_ABS_ML, "mmix-abs-ml"},
      {MMIXII::MO_ABS_MH, "mmix-abs-mh"},
      {MMIXII::MO_ABS_HI, "mmix-abs-hi"},
      {MMIXII::MO_TPREL_LO, "mmix-tprel-lo"},
      {MMIXII::MO_TPREL_ML, "mmix-tprel-ml"},
      {MMIXII::MO_TPREL_MH, "mmix-tprel-mh"},
      {MMIXII::MO_TPREL_HI, "mmix-tprel-hi"},
  };
  return ArrayRef(TargetFlags);
}

std::pair<unsigned, unsigned>
MMIXInstrInfo::decomposeMachineOperandsTargetFlags(unsigned TF) const {
  return std::make_pair(TF, 0u);
}

static bool isMMIXBranch(unsigned Opcode) {
  return Opcode == MMIX::PseudoB || Opcode == MMIX::PseudoJMP;
}

bool MMIXInstrInfo::analyzeBranch(
    MachineBasicBlock &MBB, MachineBasicBlock *&TBB, MachineBasicBlock *&FBB,
    SmallVectorImpl<MachineOperand> &Cond, bool AllowModify) const {
  auto I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (!isUnpredicatedTerminator(*I))
      break;
    if (!isMMIXBranch(I->getOpcode()))
      return true;

    if (I->getOpcode() == MMIX::PseudoJMP) {
      if (AllowModify) {
        MBB.erase(std::next(I), MBB.end());
        Cond.clear();
        FBB = nullptr;
        if (MBB.isLayoutSuccessor(I->getOperand(0).getMBB())) {
          TBB = nullptr;
          I->eraseFromParent();
          I = MBB.end();
          continue;
        }
      }
      TBB = I->getOperand(0).getMBB();
      continue;
    }

    if (!Cond.empty())
      return true;
    FBB = TBB;
    TBB = I->getOperand(1).getMBB();
    Cond.push_back(I->getOperand(0));
    Cond.push_back(I->getOperand(2));
  }
  return false;
}

unsigned MMIXInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                     int *BytesRemoved) const {
  unsigned Count = 0;
  auto I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (!isMMIXBranch(I->getOpcode()))
      break;
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }
  if (BytesRemoved)
    *BytesRemoved = Count * 4;
  return Count;
}

unsigned MMIXInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                     MachineBasicBlock *TBB,
                                     MachineBasicBlock *FBB,
                                     ArrayRef<MachineOperand> Cond,
                                     const DebugLoc &DL,
                                     int *BytesAdded) const {
  assert(TBB && "cannot insert a fallthrough branch");
  unsigned Count = 1;
  if (Cond.empty()) {
    assert(!FBB && "unconditional branch cannot have a false target");
    BuildMI(&MBB, DL, get(MMIX::PseudoJMP)).addMBB(TBB);
  } else {
    assert(Cond.size() == 2 && Cond[0].isReg() && Cond[1].isImm() &&
           "invalid MMIX branch condition");
    BuildMI(&MBB, DL, get(MMIX::PseudoB))
        .add(Cond[0])
        .addMBB(TBB)
        .addImm(Cond[1].getImm());
    if (FBB) {
      BuildMI(&MBB, DL, get(MMIX::PseudoJMP)).addMBB(FBB);
      ++Count;
    }
  }
  if (BytesAdded)
    *BytesAdded = Count * 4;
  return Count;
}

bool MMIXInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  if (Cond.size() != 2 || !Cond[1].isImm())
    return true;
  if (Cond[1].getImm() < 0 || Cond[1].getImm() > 7)
    return true;
  Cond[1].setImm(Cond[1].getImm() ^ 4);
  return false;
}

bool MMIXInstrInfo::isBranchOffsetInRange(unsigned BranchOpc,
                                          int64_t BrOffset) const {
  int64_t MaxOffset;
  if (BranchOpc == MMIX::PseudoB)
    MaxOffset = 0xffff * 4;
  else if (BranchOpc == MMIX::PseudoJMP)
    MaxOffset = 0xffffff * 4;
  else
    llvm_unreachable("unknown MMIX CodeGen branch");
  return BrOffset >= -MaxOffset && BrOffset <= MaxOffset;
}

unsigned MMIXInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  if (MI.getOpcode() == TargetOpcode::INLINEASM ||
      MI.getOpcode() == TargetOpcode::INLINEASM_BR) {
    const MachineFunction *MF = MI.getParent()->getParent();
    return getInlineAsmLength(MI.getOperand(0).getSymbolName(),
                              MF->getTarget().getMCAsmInfo());
  }
  if (MI.getOpcode() == TargetOpcode::BUNDLE)
    return getInstBundleSize(MI);
  return MI.getDesc().getSize();
}

MachineBasicBlock *
MMIXInstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  if (MI.getOpcode() == MMIX::PseudoB)
    return MI.getOperand(1).getMBB();
  if (MI.getOpcode() == MMIX::PseudoJMP)
    return MI.getOperand(0).getMBB();
  return nullptr;
}

void MMIXInstrInfo::insertIndirectBranch(
    MachineBasicBlock &, MachineBasicBlock &, MachineBasicBlock &,
    const DebugLoc &, int64_t BrOffset, RegScavenger *) const {
  // Current MMIX CodeGen only has direct block branches. Address
  // materialization for a function larger than JMP/JMPB's 24-bit word reach
  // belongs to the later relocation and code-model work.
  report_fatal_error(Twine("MMIX function exceeds the 24-bit direct branch "
                           "range: ") +
                     Twine(BrOffset));
}

void MMIXInstrInfo::adjustReg(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MBBI,
                              const DebugLoc &DL, Register DstReg,
                              Register SrcReg, int64_t Amount,
                              MachineInstr::MIFlag Flags) const {
  if (Amount == 0) {
    if (DstReg != SrcReg)
      BuildMI(MBB, MBBI, DL, get(MMIX::ORI), DstReg)
          .addReg(SrcReg)
          .addImm(0)
          .setMIFlag(Flags);
    return;
  }

  bool Subtract = Amount < 0;
  uint64_t Magnitude = Subtract ? 0 - uint64_t(Amount) : uint64_t(Amount);
  if (Magnitude <= 255) {
    BuildMI(MBB, MBBI, DL, get(Subtract ? MMIX::SUBUI : MMIX::ADDUI), DstReg)
        .addReg(SrcReg)
        .addImm(Magnitude)
        .setMIFlag(Flags);
    return;
  }

  loadImmediate(MBB, MBBI, DL, MMIX::R255, Magnitude, Flags);
  BuildMI(MBB, MBBI, DL, get(Subtract ? MMIX::SUBU : MMIX::ADDU), DstReg)
      .addReg(SrcReg)
      .addReg(MMIX::R255, RegState::Kill)
      .setMIFlag(Flags);
}

void MMIXInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MBBI,
                                const DebugLoc &DL, Register DstReg,
                                Register SrcReg, bool KillSrc,
                                bool RenamableDest, bool RenamableSrc) const {
  if (!MMIX::GPR64RegClass.contains(DstReg, SrcReg))
    report_fatal_error("MMIX cannot copy between these register classes");

  BuildMI(MBB, MBBI, DL, get(MMIX::ORI))
      .addReg(DstReg, RegState::Define | getRenamableRegState(RenamableDest))
      .addReg(SrcReg,
              getKillRegState(KillSrc) | getRenamableRegState(RenamableSrc))
      .addImm(0);
}

static bool isOctaSpillClass(const TargetRegisterClass *RC) {
  // FPR64 holds the full 64-bit floating representation. LDSF and STSF
  // perform conversions and therefore cannot preserve FPR64 values or the
  // raw low-tetra encoding held by F32BitsCodeGen.
  return MMIX::GPR64RegClass.hasSubClassEq(RC) ||
         MMIX::FPR64RegClass.hasSubClassEq(RC) ||
         MMIX::F32BitsCodeGenRegClass.hasSubClassEq(RC);
}

void MMIXInstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MBBI,
                                        Register SrcReg, bool IsKill,
                                        int FrameIndex,
                                        const TargetRegisterClass *RC, Register,
                                        MachineInstr::MIFlag Flags) const {
  if (!isOctaSpillClass(RC))
    report_fatal_error("MMIX cannot spill this register class");

  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIndex),
      MachineMemOperand::MOStore, MFI.getObjectSize(FrameIndex),
      MFI.getObjectAlign(FrameIndex));

  BuildMI(MBB, MBBI, DebugLoc(), get(MMIX::STOUI))
      .addReg(SrcReg, getKillRegState(IsKill))
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .addMemOperand(MMO)
      .setMIFlag(Flags);
}

void MMIXInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MBBI,
                                         Register DstReg, int FrameIndex,
                                         const TargetRegisterClass *RC,
                                         Register, unsigned SubReg,
                                         MachineInstr::MIFlag Flags) const {
  if (SubReg)
    report_fatal_error("MMIX does not support partial register reloads");
  if (!isOctaSpillClass(RC))
    report_fatal_error("MMIX cannot reload this register class");

  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIndex),
      MachineMemOperand::MOLoad, MFI.getObjectSize(FrameIndex),
      MFI.getObjectAlign(FrameIndex));

  BuildMI(MBB, MBBI, DebugLoc(), get(MMIX::LDOUI), DstReg)
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .addMemOperand(MMO)
      .setMIFlag(Flags);
}

Register MMIXInstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                            int &FrameIndex) const {
  TypeSize MemBytes = TypeSize::getZero();
  return isLoadFromStackSlot(MI, FrameIndex, MemBytes);
}

Register MMIXInstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                            int &FrameIndex,
                                            TypeSize &MemBytes) const {
  if (MI.getOpcode() != MMIX::LDOUI || !MI.getOperand(1).isFI() ||
      !MI.getOperand(2).isImm() || MI.getOperand(2).getImm() != 0)
    return Register();

  FrameIndex = MI.getOperand(1).getIndex();
  MemBytes = TypeSize::getFixed(8);
  return MI.getOperand(0).getReg();
}

Register MMIXInstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                           int &FrameIndex) const {
  TypeSize MemBytes = TypeSize::getZero();
  return isStoreToStackSlot(MI, FrameIndex, MemBytes);
}

Register MMIXInstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                           int &FrameIndex,
                                           TypeSize &MemBytes) const {
  if (MI.getOpcode() != MMIX::STOUI || !MI.getOperand(1).isFI() ||
      !MI.getOperand(2).isImm() || MI.getOperand(2).getImm() != 0)
    return Register();

  FrameIndex = MI.getOperand(1).getIndex();
  MemBytes = TypeSize::getFixed(8);
  return MI.getOperand(0).getReg();
}
