//===-- MMIXAsmPrinter.cpp - MMIX assembly printer -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MMIXALAsmStreamer.h"
#include "MCTargetDesc/MMIXALStartup.h"
#include "MCTargetDesc/MMIXBaseInfo.h"
#include "MCTargetDesc/MMIXInstPrinter.h"
#include "MCTargetDesc/MMIXMCTargetDesc.h"
#include "MMIXALModuleValidator.h"
#include "MMIXAddressEmission.h"
#include "MMIXMCInstLower.h"
#include "TargetInfo/MMIXTargetInfo.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

namespace {

bool isReviewedSymbolicIntegerWidth(unsigned BitWidth) {
  switch (BitWidth) {
  case 8:
  case 16:
  case 32:
  case 64:
    return true;
  default:
    return false;
  }
}

bool constantReferencesSymbol(const Constant &C,
                              SmallPtrSetImpl<const Constant *> &Visited) {
  if (isa<GlobalValue, BlockAddress>(C))
    return true;
  if (!Visited.insert(&C).second)
    return false;

  for (const Value *Operand : C.operands())
    if (const auto *Nested = dyn_cast<Constant>(Operand);
        Nested && constantReferencesSymbol(*Nested, Visited))
      return true;
  return false;
}

Error validateSymbolicInitializerStorage(
    const Constant &C, const GlobalVariable &Global,
    SmallPtrSetImpl<const Constant *> &Visited) {
  if (!Visited.insert(&C).second || isa<GlobalValue, BlockAddress>(C))
    return Error::success();

  if (const auto *Integer = dyn_cast<IntegerType>(C.getType());
      Integer && !isReviewedSymbolicIntegerWidth(Integer->getBitWidth())) {
    SmallPtrSet<const Constant *, 8> SymbolVisited;
    if (constantReferencesSymbol(C, SymbolVisited))
      return createStringError(
          Twine("MMIX symbolic initializer for global '") + Global.getName() +
          "' uses unreviewed i" + Twine(Integer->getBitWidth()) +
          " storage; supported integer widths are i8, i16, i32, and i64");
  }

  for (const Value *Operand : C.operands())
    if (const auto *Nested = dyn_cast<Constant>(Operand))
      if (Error Err =
              validateSymbolicInitializerStorage(*Nested, Global, Visited))
        return Err;
  return Error::success();
}

Error validateSymbolicInitializerStorage(const Module &M) {
  for (const GlobalVariable &Global : M.globals()) {
    if (!Global.hasInitializer())
      continue;
    SmallPtrSet<const Constant *, 8> Visited;
    if (Error Err = validateSymbolicInitializerStorage(*Global.getInitializer(),
                                                       Global, Visited))
      return Err;
  }
  return Error::success();
}

class MMIXAsmPrinter final : public AsmPrinter {
  MMIXALAsmStreamer *MMIXALStreamer;
  MMIXEmissionMode EmissionMode;
  const Function *MMIXALRawEntry = nullptr;

  void reportSymbolRegistrationError(Error Err) {
    if (Err)
      MMIXALStreamer->recordModuleError(toString(std::move(Err)));
  }

  void registerModuleSymbols(Module &M, const Function &RawEntry) {
    if (!MMIXALStreamer)
      return;

    for (Function &F : M) {
      if (!F.isIntrinsic()) {
        if (&F == &RawEntry)
          reportSymbolRegistrationError(
              MMIXALStreamer->registerEntrySymbol(*getSymbol(&F), F.getName()));
        else
          reportSymbolRegistrationError(
              MMIXALStreamer->registerUserSymbol(*getSymbol(&F), F.getName()));
      }
    }
    for (GlobalVariable &Global : M.globals())
      reportSymbolRegistrationError(MMIXALStreamer->registerUserSymbol(
          *getSymbol(&Global), Global.getName()));
    for (GlobalAlias &Alias : M.aliases())
      reportSymbolRegistrationError(MMIXALStreamer->registerUserSymbol(
          *getSymbol(&Alias), Alias.getName()));
  }

  void registerFunctionSymbols(MachineFunction &MF) {
    if (!MMIXALStreamer)
      return;

    using Kind = MMIXALSymbolTable::PrivateSymbolKind;
    const Function &F = MF.getFunction();
    const StringRef FunctionName = F.getName();
    reportSymbolRegistrationError(
        MMIXALStreamer->beginFunctionSymbols(FunctionName));

    uint64_t BlockAddressOrdinal = 0;
    for (const BasicBlock &BB : F)
      if (BB.hasAddressTaken())
        reportSymbolRegistrationError(
            MMIXALStreamer->registerFunctionPrivateSymbol(
                *GetBlockAddressSymbol(&BB), FunctionName, Kind::BlockAddress,
                BlockAddressOrdinal++));

    SmallPtrSet<const BasicBlock *, 8> RegisteredSourceBlocks;
    uint64_t BasicBlockOrdinal = 0;
    for (MachineBasicBlock &MBB : MF) {
      reportSymbolRegistrationError(
          MMIXALStreamer->registerFunctionPrivateSymbol(
              *MBB.getSymbol(), FunctionName, Kind::BasicBlock,
              BasicBlockOrdinal++));

      const BasicBlock *BB = MBB.getBasicBlock();
      if (!BB || !BB->hasName() || !RegisteredSourceBlocks.insert(BB).second)
        continue;
      const bool IsRawEntryBlock = &F == MMIXALRawEntry && MBB.isEntryBlock();
      if (IsRawEntryBlock)
        MBB.setLabelMustBeEmitted();
      const MCSymbol *AddressSymbol =
          BB->hasAddressTaken() ? GetBlockAddressSymbol(BB) : MBB.getSymbol();
      if (!BB->hasAddressTaken() && !IsRawEntryBlock && MBB.isEntryBlock())
        AddressSymbol = CurrentFnSym;
      reportSymbolRegistrationError(MMIXALStreamer->registerSourceBlock(
          *AddressSymbol, FunctionName, BB->getName()));
    }

    const auto &Constants = MF.getConstantPool()->getConstants();
    for (uint64_t I = 0; I != Constants.size(); ++I)
      reportSymbolRegistrationError(
          MMIXALStreamer->registerFunctionPrivateSymbol(
              *GetCPISymbol(I), FunctionName, Kind::ConstantPool, I));

    if (const MachineJumpTableInfo *MJTI = MF.getJumpTableInfo())
      for (uint64_t I = 0; I != MJTI->getJumpTables().size(); ++I)
        reportSymbolRegistrationError(
            MMIXALStreamer->registerFunctionPrivateSymbol(
                *GetJTISymbol(I), FunctionName, Kind::JumpTable, I));
  }

  void emitCheckedMCInstruction(const MCInst &Inst) {
    if (!MMIX_MC::isOpcodeAvailable(Inst.getOpcode(),
                                    getSubtargetInfo().getFeatureBits()))
      report_fatal_error(Twine("cannot emit ") +
                         TM.getMCInstrInfo()->getName(Inst.getOpcode()) +
                         ": required target feature is disabled");
    MMIX_MC::verifyInstructionPredicates(Inst.getOpcode(),
                                         getSubtargetInfo().getFeatureBits());
    EmitToStreamer(*OutStreamer, Inst);
  }

public:
  explicit MMIXAsmPrinter(TargetMachine &TM,
                          std::unique_ptr<MCStreamer> Streamer,
                          MMIXEmissionMode EmissionMode,
                          MMIXALAsmStreamer *MMIXALStreamer = nullptr)
      : AsmPrinter(TM, std::move(Streamer)), MMIXALStreamer(MMIXALStreamer),
        EmissionMode(EmissionMode) {}

  StringRef getPassName() const override { return "MMIX Assembly Printer"; }

  void emitStartOfAsmFile(Module &M) override {
    if (Error Err = validateSymbolicInitializerStorage(M)) {
      std::string Message = toString(std::move(Err));
      report_fatal_error(StringRef(Message));
    }

    if (!MMIXALStreamer)
      return;

    Expected<const Function *> Entry = validateMMIXALRawEntry(M);
    if (!Entry) {
      MMIXALStreamer->recordModuleError(toString(Entry.takeError()));
      return;
    }
    MMIXALRawEntry = *Entry;
    registerModuleSymbols(M, **Entry);
    addMMIXALBareMetalGlobalRegisterPrelude(*MMIXALStreamer);
  }

  void emitFunctionBodyStart() override {
    if (!MMIXALStreamer || &MF->getFunction() != MMIXALRawEntry)
      return;
    for (const MCInst &Inst : createMMIXALRawEntryPrefix())
      emitCheckedMCInstruction(Inst);
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    SetupMachineFunction(MF);
    registerFunctionSymbols(MF);
    emitFunctionBody();
    if (MMIXALStreamer)
      reportSymbolRegistrationError(
          MMIXALStreamer->endFunctionSymbols(getFunctionEnd()));
    return false;
  }

  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &OS) override {
    if (!AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, OS))
      return false;
    if (ExtraCode && ExtraCode[0])
      return true;

    const MachineOperand &MO = MI->getOperand(OpNo);
    if (MO.isReg()) {
      if (!MO.getReg())
        return true;
      OS << MMIXInstPrinter::getRegisterName(MO.getReg());
      return false;
    }
    if (MO.isImm()) {
      OS << MO.getImm();
      return false;
    }
    if (MO.isGlobal()) {
      PrintSymbolOperand(MO, OS);
      return false;
    }
    if (MO.isMBB()) {
      MO.getMBB()->getSymbol()->print(OS, MAI);
      return false;
    }
    if (MO.isBlockAddress()) {
      GetBlockAddressSymbol(MO.getBlockAddress())->print(OS, MAI);
      return false;
    }
    if (MO.isSymbol()) {
      GetExternalSymbolSymbol(MO.getSymbolName())->print(OS, MAI);
      return false;
    }
    return true;
  }

  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &OS) override {
    if (ExtraCode && ExtraCode[0])
      return true;
    if (OpNo + 1 >= MI->getNumOperands())
      return true;

    const MachineOperand &Base = MI->getOperand(OpNo);
    const MachineOperand &Offset = MI->getOperand(OpNo + 1);
    if (!Base.isReg() || !Base.getReg())
      return true;
    if ((!Offset.isReg() || !Offset.getReg()) && !Offset.isImm())
      return true;

    OS << MMIXInstPrinter::getRegisterName(Base.getReg()) << ", ";
    if (Offset.isReg())
      OS << MMIXInstPrinter::getRegisterName(Offset.getReg());
    else
      OS << Offset.getImm();
    return false;
  }

  void emitInstruction(const MachineInstr *MI) override {
    if (MI->getOpcode() == MMIX::LOAD_CALL_ADDR) {
      // Materialize unresolved callees even in ELF: section-end call stubs
      // need not be reachable from a call in a large input section.
      if (!MI->getOperand(0).isReg())
        report_fatal_error("MMIX call address has no destination register");
      MMIXMCInstLower Lower(OutContext, *this, EmissionMode);
      const MCExpr *Address = Lower.lowerAddressOperand(MI->getOperand(1));
      for (const MCInst &Inst : createMMIXStaticAddressSequence(
               EmissionMode, MI->getOperand(0).getReg(), Address, OutContext))
        emitCheckedMCInstruction(Inst);
      return;
    }

    if (MI->getOpcode() == MMIX::LOAD_ADDR) {
      if (!MI->getOperand(0).isReg())
        report_fatal_error("MMIX static address has no destination register");
      MMIXMCInstLower Lower(OutContext, *this, EmissionMode);
      const MCExpr *Address = Lower.lowerAddressOperand(MI->getOperand(1));
      for (const MCInst &Inst : createMMIXStaticAddressSequence(
               EmissionMode, MI->getOperand(0).getReg(), Address, OutContext))
        emitCheckedMCInstruction(Inst);
      return;
    }

    if (MI->isPseudo() && MI->getOpcode() != MMIX::PseudoB &&
        MI->getOpcode() != MMIX::PseudoJMP &&
        MI->getOpcode() != MMIX::PseudoPUSHJ &&
        MI->getOpcode() != MMIX::PseudoPUSHGO &&
        MI->getOpcode() != MMIX::PseudoDirectCall &&
        MI->getOpcode() != MMIX::PseudoDirectTail &&
        MI->getOpcode() != MMIX::PseudoMaterializedDirectTail &&
        MI->getOpcode() != MMIX::PseudoIndirectTail)
      report_fatal_error(
          "MMIX CodeGen pseudo reached canonical assembly emission");

    MCInst OutMI;
    MMIXMCInstLower(OutContext, *this, EmissionMode).lower(*MI, OutMI);
    emitCheckedMCInstruction(OutMI);
  }
};

} // namespace

static AsmPrinter *
createMMIXAsmPrinter(TargetMachine &TM,
                     std::unique_ptr<MCStreamer> &&Streamer) {
  if (TM.getMCAsmInfo().getOutputAssemblerDialect() ==
      MMIXII::MMIXALAsmVariant) {
    auto *MMIXALStreamer = static_cast<MMIXALAsmStreamer *>(Streamer.get());
    MMIXALStreamer->beginModuleEmission();
    return new MMIXAsmPrinter(TM, std::move(Streamer),
                              MMIXEmissionMode::MMIXALAssembly, MMIXALStreamer);
  }
  const MMIXEmissionMode Mode = Streamer->hasRawTextSupport()
                                    ? MMIXEmissionMode::CanonicalAssembly
                                    : MMIXEmissionMode::ELFObject;
  return new MMIXAsmPrinter(TM, std::move(Streamer), Mode);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeMMIXAsmPrinter() {
  TargetRegistry::RegisterAsmPrinter(getTheMMIXTarget(), createMMIXAsmPrinter);
}
