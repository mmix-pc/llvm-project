//===-- MMIXTargetObjectFile.h - MMIX object lowering ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MMIX_MMIXTARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_MMIX_MMIXTARGETOBJECTFILE_H

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
class MMIXTargetObjectFile : public TargetLoweringObjectFileELF {
public:
  void Initialize(MCContext &Ctx, const TargetMachine &TM) override {
    TargetLoweringObjectFileELF::Initialize(Ctx, TM);
    if (TM.getTargetTriple().isOSLinux() && TM.isPositionIndependent()) {
      // Full-width relative fields avoid dynamic writes to read-only unwind
      // metadata. Interposable pointers live in the generic writable stubs.
      FDECFIEncoding = dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata8;
      LSDAEncoding = FDECFIEncoding;
      PersonalityEncoding = FDECFIEncoding | dwarf::DW_EH_PE_indirect;
      TTypeEncoding = PersonalityEncoding;
      return;
    }
    // Match the MC path without limiting static code to signed PC-relative reach.
    FDECFIEncoding = dwarf::DW_EH_PE_absptr;
    // Static exception metadata must also reach the full MMIX address space.
    PersonalityEncoding = dwarf::DW_EH_PE_absptr;
    LSDAEncoding = dwarf::DW_EH_PE_absptr;
    TTypeEncoding = dwarf::DW_EH_PE_absptr;
  }
};
} // namespace llvm

#endif
