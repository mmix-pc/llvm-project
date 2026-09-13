//===-- MMIXMachineFunctionInfo.h - MMIX machine function info -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MMIX_MMIXMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_MMIX_MMIXMACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

#include <cassert>
#include <limits>

namespace llvm {

class MMIXMachineFunctionInfo final : public MachineFunctionInfo {
  unsigned IncomingStackArgSize = 0;
  unsigned NamedArgSlots = 0;
  unsigned FirstVarArgRegisterIndex = 0;
  unsigned VarArgsSaveSize = 0;
  int VarArgsFrameIndex = std::numeric_limits<int>::max();
  int UnwindReturnAddressFrameIndex = std::numeric_limits<int>::max();
  Register ReturnAddressRegister;

public:
  MMIXMachineFunctionInfo(const Function &, const TargetSubtargetInfo *) {}

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;

  void setIncomingStackArgSize(unsigned Size) {
    assert(Size % 8 == 0 && "unaligned MMIX incoming stack argument area");
    IncomingStackArgSize = Size;
  }
  unsigned getIncomingStackArgSize() const { return IncomingStackArgSize; }

  void setVarArgsInfo(unsigned Slots, unsigned FirstRegister, unsigned SaveSize,
                      int FrameIndex) {
    assert(!hasVarArgsFrameIndex() && "MMIX varargs info is already set");
    assert(FirstRegister <= 16 && "invalid MMIX argument register index");
    assert(FirstRegister == (Slots < 16 ? Slots : 16) &&
           "inconsistent MMIX named argument slots");
    assert(SaveSize == (16 - FirstRegister) * 8 &&
           "invalid MMIX varargs save size");
    NamedArgSlots = Slots;
    FirstVarArgRegisterIndex = FirstRegister;
    VarArgsSaveSize = SaveSize;
    VarArgsFrameIndex = FrameIndex;
  }

  unsigned getNamedArgSlots() const { return NamedArgSlots; }
  unsigned getFirstVarArgRegisterIndex() const {
    return FirstVarArgRegisterIndex;
  }
  unsigned getVarArgsSaveSize() const { return VarArgsSaveSize; }
  int getVarArgsFrameIndex() const {
    assert(hasVarArgsFrameIndex() && "MMIX varargs frame index is not set");
    return VarArgsFrameIndex;
  }
  bool hasVarArgsFrameIndex() const {
    return VarArgsFrameIndex != std::numeric_limits<int>::max();
  }

  void setUnwindReturnAddressFrameIndex(int FrameIndex) {
    assert(!hasUnwindReturnAddressFrameIndex() &&
           "MMIX unwind return-address frame index is already set");
    UnwindReturnAddressFrameIndex = FrameIndex;
  }
  int getUnwindReturnAddressFrameIndex() const {
    assert(hasUnwindReturnAddressFrameIndex() &&
           "MMIX unwind return-address frame index is not set");
    return UnwindReturnAddressFrameIndex;
  }
  bool hasUnwindReturnAddressFrameIndex() const {
    return UnwindReturnAddressFrameIndex != std::numeric_limits<int>::max();
  }

  Register getOrCreateReturnAddressRegister(MachineFunction &MF);
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_MMIX_MMIXMACHINEFUNCTIONINFO_H
