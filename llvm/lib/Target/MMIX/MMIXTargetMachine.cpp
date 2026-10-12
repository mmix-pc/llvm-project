//===-- MMIXTargetMachine.cpp - Define TargetMachine for MMIX -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MMIXTargetMachine.h"
#include "MCTargetDesc/MMIXBaseInfo.h"
#include "MMIX.h"
#include "MMIXALModuleValidator.h"
#include "MMIXMachineFunctionInfo.h"
#include "MMIXTargetObjectFile.h"
#include "MMIXTargetTransformInfo.h"
#include "TargetInfo/MMIXTargetInfo.h"
#include "llvm/CodeGen/AtomicExpand.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM,
                                         const Triple &TT,
                                         const TargetOptions &Options) {
  Reloc::Model Model = RM.value_or(Reloc::Static);
  if (Model == Reloc::PIC_ && TT.isOSLinux() &&
      Options.MCOptions.OutputAsmVariant.value_or(MMIXII::CanonicalAsmVariant) ==
          MMIXII::CanonicalAsmVariant)
    return Model;
  if (Model != Reloc::Static)
    report_fatal_error("MMIX supports only the static relocation model");
  return Model;
}

static CodeModel::Model
getMMIXEffectiveCodeModel(std::optional<CodeModel::Model> CM) {
  CodeModel::Model Model = CM.value_or(CodeModel::Small);
  if (Model != CodeModel::Small)
    report_fatal_error("MMIX supports only the small code model");
  return Model;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMMIXTarget() {
  RegisterTargetMachine<MMIXTargetMachine> X(getTheMMIXTarget());
  initializeMMIXDAGToDAGISelLegacyPass(*PassRegistry::getPassRegistry());
}

MMIXTargetMachine::MMIXTargetMachine(const Target &T, const Triple &TT,
                                     StringRef CPU, StringRef FS,
                                     const TargetOptions &Options,
                                     std::optional<Reloc::Model> RM,
                                     std::optional<CodeModel::Model> CM,
                                     CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(
          T, TT.computeDataLayout(), TT, CPU, FS, Options,
          getEffectiveRelocModel(RM, TT, Options),
          getMMIXEffectiveCodeModel(CM), OL),
      TLOF(std::make_unique<MMIXTargetObjectFile>()),
      Subtarget(TT, CPU, FS, *this) {
  if (Options.ExceptionModel != ExceptionHandling::None &&
      Options.ExceptionModel != ExceptionHandling::DwarfCFI)
    reportFatalUsageError("MMIX supports only the DWARF exception model");
  this->Options.EnableCFIFixup = true;
  initAsmInfo();
  // MMIXAL has no unwind-table representation; its validator rejects EH IR.
  if (getMCAsmInfo().getOutputAssemblerDialect() == MMIXII::MMIXALAsmVariant)
    const_cast<MCAsmInfo &>(getMCAsmInfo())
        .setExceptionsType(ExceptionHandling::None);
}

MMIXTargetMachine::~MMIXTargetMachine() = default;

TargetTransformInfo
MMIXTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(std::make_unique<MMIXTTIImpl>(this, F));
}

bool MMIXTargetMachine::addPassesToEmitFile(
    PassManagerBase &PM, raw_pwrite_stream &Out, raw_pwrite_stream *DwoOut,
    CodeGenFileType FileType, bool DisableVerify,
    MachineModuleInfoWrapperPass *MMIWP) {
  if (FileType == CodeGenFileType::ObjectFile &&
      getMCAsmInfo().getOutputAssemblerDialect() == MMIXII::MMIXALAsmVariant)
    return true;
  return CodeGenTargetMachineImpl::addPassesToEmitFile(
      PM, Out, DwoOut, FileType, DisableVerify, MMIWP);
}

namespace {

class MMIXPassConfig final : public TargetPassConfig {
public:
  MMIXPassConfig(MMIXTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  MMIXTargetMachine &getMMIXTargetMachine() const {
    return getTM<MMIXTargetMachine>();
  }

  bool addInstSelector() override {
    addPass(createMMIXISelDag(getMMIXTargetMachine()));
    return false;
  }

  void addIRPasses() override {
    if (getMMIXTargetMachine().getMCAsmInfo().getOutputAssemblerDialect() ==
        MMIXII::MMIXALAsmVariant)
      addPass(createMMIXALModuleValidatorPass());
    addPass(createAtomicExpandLegacyPass());
    TargetPassConfig::addIRPasses();
  }

  void addPreEmitPass() override {
    // Expand an out-of-range 16-bit conditional branch into an inverted nearby
    // condition plus a 24-bit JMP before final direction selection.
    addPass(&BranchRelaxationPassID);
  }
};

} // namespace

TargetPassConfig *MMIXTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new MMIXPassConfig(*this, PM);
}

MachineFunctionInfo *MMIXTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return MMIXMachineFunctionInfo::create<MMIXMachineFunctionInfo>(Allocator, F,
                                                                  STI);
}
