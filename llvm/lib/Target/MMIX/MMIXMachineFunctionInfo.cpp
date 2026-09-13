//===-- MMIXMachineFunctionInfo.cpp - MMIX machine function info ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MMIXMachineFunctionInfo.h"
#include "MCTargetDesc/MMIXMCTargetDesc.h"
#include "MMIXInstrInfo.h"
#include "MMIXSubtarget.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

Register MMIXMachineFunctionInfo::getOrCreateReturnAddressRegister(
    MachineFunction &MF) {
  if (!ReturnAddressRegister) {
    ReturnAddressRegister =
        MF.getRegInfo().createVirtualRegister(&MMIX::GPR64RegClass);
    MachineBasicBlock &Entry = MF.front();
    const auto *TII = MF.getSubtarget<MMIXSubtarget>().getInstrInfo();
    // Capture the incoming rJ before any call, including uses in later blocks.
    BuildMI(Entry, Entry.begin(), DebugLoc(), TII->get(MMIX::GET),
            ReturnAddressRegister)
        .addReg(MMIX::RJ);
  }
  return ReturnAddressRegister;
}

MachineFunctionInfo *MMIXMachineFunctionInfo::clone(
    BumpPtrAllocator &Allocator, MachineFunction &DestMF,
    const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
    const {
  return DestMF.cloneInfo<MMIXMachineFunctionInfo>(*this);
}
