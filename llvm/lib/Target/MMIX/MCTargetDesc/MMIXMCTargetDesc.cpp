//===-- MMIXMCTargetDesc.cpp - MMIX target descriptions -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MMIXMCTargetDesc.h"
#include "MMIXALAsmStreamer.h"
#include "MMIXALInstPrinter.h"
#include "MMIXBaseInfo.h"
#include "MMIXInstPrinter.h"
#include "MMIXMCAsmInfo.h"
#include "MMIXTargetStreamer.h"
#include "TargetInfo/MMIXTargetInfo.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include <array>
#include <cstdint>
#include <memory>
#include <utility>

using namespace llvm;

namespace {
class MMIXMCObjectFileInfo : public MCObjectFileInfo {
public:
  MMIXMCObjectFileInfo(MCContext &Ctx, bool PIC, bool Large) {
    initMCObjectFileInfo(Ctx, PIC, Large);
    // Static MMIX code addresses need the full unsigned address space.
    FDECFIEncoding = PIC && Ctx.getTargetTriple().isOSLinux()
                        ? dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata8
                        : dwarf::DW_EH_PE_absptr;
  }
};
} // namespace

static MCObjectFileInfo *createMMIXMCObjectFileInfo(MCContext &Ctx, bool PIC,
                                                  bool Large) {
  return new MMIXMCObjectFileInfo(Ctx, PIC, Large);
}

#define GET_SUBTARGETINFO_MC_DESC
#include "MMIXGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "MMIXGenRegisterInfo.inc"

#define GET_INSTRINFO_MC_DESC
#define GET_AVAILABLE_OPCODE_CHECKER
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "MMIXGenInstrInfo.inc"

MCInstrInfo *llvm::createMMIXMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitMMIXMCInstrInfo(X);
  return X;
}

MCRegisterInfo *llvm::createMMIXMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitMMIXMCRegisterInfo(X, MMIX::RJ);
  return X;
}

MCSubtargetInfo *llvm::createMMIXMCSubtargetInfo(const Triple &TT,
                                                 StringRef CPU, StringRef FS) {
  const StringRef CPUName = CPU.empty() ? "generic" : CPU;
  return createMMIXMCSubtargetInfoImpl(TT, CPUName, CPUName, FS);
}

static MCInstPrinter *createMMIXMCInstPrinter(const Triple &,
                                              unsigned SyntaxVariant,
                                              const MCAsmInfo &MAI,
                                              const MCInstrInfo &MII,
                                              const MCRegisterInfo &MRI) {
  switch (SyntaxVariant) {
  case MMIXII::CanonicalAsmVariant:
    return new MMIXInstPrinter(MAI, MII, MRI);
  case MMIXII::MMIXALAsmVariant:
    return new MMIXALInstPrinter(MAI, MII, MRI);
  default:
    return nullptr;
  }
}

static MCStreamer *
createMMIXAsmStreamer(MCContext &Ctx, std::unique_ptr<formatted_raw_ostream> OS,
                      std::unique_ptr<MCInstPrinter> IP,
                      std::unique_ptr<MCCodeEmitter> CE,
                      std::unique_ptr<MCAsmBackend> MAB) {
  if (Ctx.getAsmInfo().getOutputAssemblerDialect() != MMIXII::MMIXALAsmVariant)
    return llvm::createAsmStreamer(Ctx, std::move(OS), std::move(IP),
                                   std::move(CE), std::move(MAB));

  auto MMIXALPrinter = std::unique_ptr<MMIXALInstPrinter>(
      static_cast<MMIXALInstPrinter *>(IP.release()));
  return new MMIXALAsmStreamer(Ctx, std::move(OS), std::move(MMIXALPrinter),
                               std::move(CE));
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMMIXTargetMC() {
  Target &T = getTheMMIXTarget();
  RegisterMCAsmInfo<MMIXMCAsmInfo> X(T);
  TargetRegistry::RegisterMCObjectFileInfo(T, createMMIXMCObjectFileInfo);
  TargetRegistry::RegisterMCInstrInfo(T, createMMIXMCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createMMIXMCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createMMIXMCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createMMIXMCInstPrinter);
  TargetRegistry::RegisterAsmStreamer(T, createMMIXAsmStreamer);
  TargetRegistry::RegisterMCCodeEmitter(T, createMMIXMCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(T, createMMIXAsmBackend);
  TargetRegistry::RegisterObjectTargetStreamer(T,
                                               createMMIXObjectTargetStreamer);
  TargetRegistry::RegisterAsmTargetStreamer(T, createMMIXAsmTargetStreamer);
  TargetRegistry::RegisterNullTargetStreamer(T, createMMIXNullTargetStreamer);
}
