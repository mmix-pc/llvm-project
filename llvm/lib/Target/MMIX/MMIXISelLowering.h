//===-- MMIXISelLowering.h - MMIX DAG lowering interface ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MMIX_MMIXISELLOWERING_H
#define LLVM_LIB_TARGET_MMIX_MMIXISELLOWERING_H

#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {

class MMIXSubtarget;

namespace MMIXISD {

enum CacheOperation : unsigned {
  CachePreload,
  CachePrefetchForExecution,
  CachePrestore,
  CacheSyncData,
  CacheSyncInstructionAndData,
  CacheOperationEnd,
};

enum NodeType : unsigned {
  FIRST_NUMBER = ISD::BUILTIN_OP_END,
  UMUL_LOHI,
  SDIVREM,
  UDIVREM,
  LOAD_ADDR,
  LOAD_CALL_ADDR,
  FCMP,
  FEQL,
  FUN,
  FLOT,
  FLOTU,
  SFLOT,
  SFLOTU,
  FIXU,
  F32_TO_BITS,
  BITS_TO_F32,
  GET_SPECIAL_REGISTER,
  PUT_SPECIAL_REGISTER,
  CACHE_OPERATION,
  SYNC,
  UNCACHED_LOAD,
  UNCACHED_STORE,
  VIRTUAL_TRANSLATION_SEARCH,
  CALL,
  DIRECT_CALL,
  INDIRECT_TAIL,
  DIRECT_TAIL,
  RET_GLUE,
  RET_VALUE_GLUE,
  RET_PAIR_GLUE,
};

} // namespace MMIXISD

class MMIXTargetLowering final : public TargetLowering {
public:
  MMIXTargetLowering(const TargetMachine &TM, const MMIXSubtarget &STI);

  Register getExceptionPointerRegister(
      ExceptionHandling EH, const Constant *PersonalityFn) const override;
  Register getExceptionSelectorRegister(
      ExceptionHandling EH, const Constant *PersonalityFn) const override;

  AsmOperandInfoVector ParseConstraints(const DataLayout &DL,
                                        const TargetRegisterInfo *TRI,
                                        const CallBase &Call) const override;
  ConstraintType getConstraintType(StringRef Constraint) const override;
  InlineAsm::ConstraintCode
  getInlineAsmMemConstraint(StringRef ConstraintCode) const override;
  ConstraintWeight
  getSingleConstraintMatchWeight(AsmOperandInfo &Info,
                                 const char *Constraint) const override;
  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;
  void LowerAsmOperandForConstraint(SDValue Op, StringRef Constraint,
                                    std::vector<SDValue> &Ops,
                                    SelectionDAG &DAG) const override;

  bool allowsMisalignedMemoryAccesses(
      EVT VT, unsigned AddrSpace, Align Alignment,
      MachineMemOperand::Flags Flags = MachineMemOperand::MONone,
      unsigned *Fast = nullptr) const override;
  void getTgtMemIntrinsic(SmallVectorImpl<IntrinsicInfo> &Infos,
                          const CallBase &I, MachineFunction &MF,
                          unsigned Intrinsic) const override;
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;
  bool shouldInsertFencesForAtomic(const Instruction *) const override {
    return true;
  }
  Instruction *emitLeadingFence(IRBuilderBase &Builder, Instruction *Inst,
                                AtomicOrdering Ord) const override;
  AtomicExpansionKind shouldExpandAtomicLoadInIR(LoadInst *LI) const override;
  AtomicExpansionKind shouldCastAtomicLoadInIR(LoadInst *LI) const override;
  AtomicExpansionKind shouldCastAtomicStoreInIR(StoreInst *SI) const override;
  void emitExpandAtomicLoad(LoadInst *LI) const override;
  AtomicExpansionKind shouldExpandAtomicStoreInIR(StoreInst *) const override {
    return AtomicExpansionKind::Expand;
  }
  AtomicExpansionKind
  shouldExpandAtomicRMWInIR(const AtomicRMWInst *) const override {
    return AtomicExpansionKind::CmpXChg;
  }
  MVT getRegisterTypeForCallingConv(LLVMContext &Context, CallingConv::ID CC,
                                    EVT VT) const override;
  unsigned getNumRegistersForCallingConv(LLVMContext &Context,
                                         CallingConv::ID CC,
                                         EVT VT) const override;
  unsigned getVectorTypeBreakdownForCallingConv(
      LLVMContext &Context, CallingConv::ID CC, EVT VT, EVT &IntermediateVT,
      unsigned &NumIntermediates, MVT &RegisterVT) const override;
  SDValue LowerCall(CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;
  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool IsVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &DL, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;
  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context, const Type *RetTy) const override;
  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
                      SelectionDAG &DAG) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_MMIX_MMIXISELLOWERING_H
