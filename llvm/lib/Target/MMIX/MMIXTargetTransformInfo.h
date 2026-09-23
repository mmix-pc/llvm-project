//===-- MMIXTargetTransformInfo.h - MMIX specific TTI ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MMIX_MMIXTARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_MMIX_MMIXTARGETTRANSFORMINFO_H

#include "MMIXSubtarget.h"
#include "MMIXTargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/IR/Function.h"

namespace llvm {

class MMIXTTIImpl final : public BasicTTIImplBase<MMIXTTIImpl> {
  using BaseT = BasicTTIImplBase<MMIXTTIImpl>;

  friend BaseT;

  const MMIXSubtarget *ST;
  const MMIXTargetLowering *TLI;

  const MMIXSubtarget *getST() const { return ST; }
  const MMIXTargetLowering *getTLI() const { return TLI; }

public:
  explicit MMIXTTIImpl(const MMIXTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}

  bool areTypesABICompatible(const Function *Caller, const Function *Callee,
                            ArrayRef<Type *> Types) const override {
    if (!BaseT::areTypesABICompatible(Caller, Callee, Types))
      return false;
    // Argument promotion must not introduce values that call lowering rejects.
    return llvm::all_of(Types, [&](Type *Ty) {
      return MMIXTargetLowering::isSupportedABIType(Ty, Caller->getDataLayout());
    });
  }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_MMIX_MMIXTARGETTRANSFORMINFO_H
