//===- MMIXLinuxRegisterTest.cpp - MMIX Linux register contract -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MMIXMCTargetDesc.h"
#include "MMIXRegisterInfo.h"
#include "MMIXSubtarget.h"
#include "MMIXTargetMachine.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "gtest/gtest.h"

using namespace llvm;

TEST(MMIXLinuxRegisters, PreservesTPOnlyInLinuxCallMasks) {
  LLVMInitializeMMIXTargetInfo();
  LLVMInitializeMMIXTarget();
  LLVMInitializeMMIXTargetMC();
  for (StringRef Name : {"mmix-unknown-linux", "mmix-unknown-linux-unknown",
                         "mmix-unknown-unknown"}) {
    Triple TT(Name);
    std::string Error;
    const Target *T = TargetRegistry::lookupTarget("", TT, Error);
    ASSERT_NE(T, nullptr) << Error;
    std::unique_ptr<TargetMachine> TM(T->createTargetMachine(
        TT, "generic", "", TargetOptions(), Reloc::Static));
    ASSERT_NE(TM, nullptr);
    LLVMContext Ctx;
    Module M("registers", Ctx);
    M.setDataLayout(TM->createDataLayout());
    Function *F = Function::Create(FunctionType::get(Type::getVoidTy(Ctx), false),
                                   GlobalValue::ExternalLinkage, "f", M);
    MachineModuleInfo MMI(TM.get());
    MachineFunction &MF = MMI.getOrCreateMachineFunction(*F);
    const auto *TRI = MF.getSubtarget().getRegisterInfo();
    EXPECT_TRUE(TRI->getReservedRegs(MF).test(MMIX::R230));
    for (CallingConv::ID CC : {CallingConv::C, CallingConv::Fast}) {
      const uint32_t *Mask = TRI->getCallPreservedMask(MF, CC);
      ASSERT_NE(Mask, nullptr);
      auto Preserved = [&](unsigned Reg) {
        return bool(Mask[Reg / 32] & (uint32_t(1) << (Reg % 32)));
      };
      EXPECT_EQ(Preserved(MMIX::R230), TT.isOSLinux());
      for (unsigned Reg : {MMIX::RG, MMIX::R254, MMIX::R253, MMIX::R0})
        EXPECT_TRUE(Preserved(Reg));
      EXPECT_FALSE(Preserved(MMIX::R231));
    }
  }
}
