//===-- MMIXStrictFP.cpp - Constrained floating-point lowering -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MMIXISelLowering.h"
#include "MCTargetDesc/MMIXMCTargetDesc.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/SelectionDAG.h"

using namespace llvm;

void MMIXTargetLowering::configureStrictFP() {
  IsStrictFPEnabled = true;
  for (unsigned Opcode :
       {ISD::STRICT_FADD, ISD::STRICT_FSUB, ISD::STRICT_FMUL, ISD::STRICT_FDIV,
        ISD::STRICT_FSQRT, ISD::STRICT_FTRUNC, ISD::STRICT_FCEIL,
        ISD::STRICT_FFLOOR, ISD::STRICT_FROUNDEVEN, ISD::STRICT_FRINT,
        ISD::STRICT_FSETCC, ISD::STRICT_FSETCCS, ISD::STRICT_FP_ROUND,
        ISD::STRICT_FP_EXTEND})
    setOperationAction(Opcode, {MVT::f32, MVT::f64}, Custom);
  for (unsigned Opcode : {ISD::STRICT_SINT_TO_FP, ISD::STRICT_UINT_TO_FP,
                          ISD::STRICT_FP_TO_SINT, ISD::STRICT_FP_TO_UINT})
    setOperationAction(Opcode, {MVT::i64, MVT::i128}, Custom);
  for (unsigned Opcode : {ISD::STRICT_FMINIMUM, ISD::STRICT_FMAXIMUM,
                          ISD::STRICT_FPOWI})
    setOperationAction(Opcode, {MVT::f32, MVT::f64}, Custom);
  for (unsigned Opcode : {ISD::STRICT_LRINT, ISD::STRICT_LLRINT,
                          ISD::STRICT_LROUND, ISD::STRICT_LLROUND})
    setOperationAction(Opcode, {MVT::f32, MVT::f64}, LibCall);
  for (unsigned Opcode :
       {ISD::STRICT_FREM,       ISD::STRICT_FMA,    ISD::STRICT_FROUND,
        ISD::STRICT_FNEARBYINT, ISD::STRICT_FACOS,  ISD::STRICT_FASIN,
        ISD::STRICT_FATAN,      ISD::STRICT_FATAN2, ISD::STRICT_FCOS,
        ISD::STRICT_FCOSH,      ISD::STRICT_FEXP,   ISD::STRICT_FEXP2,
        ISD::STRICT_FLOG,       ISD::STRICT_FLOG10, ISD::STRICT_FLOG2,
        ISD::STRICT_FPOW,       ISD::STRICT_FSIN,   ISD::STRICT_FSINH,
        ISD::STRICT_FTAN,       ISD::STRICT_FTANH,  ISD::STRICT_FLDEXP,
        ISD::STRICT_FMINNUM,    ISD::STRICT_FMAXNUM})
    setOperationAction(Opcode, {MVT::f32, MVT::f64}, LibCall);
}

void MMIXTargetLowering::ReplaceNodeResults(
    SDNode *N, SmallVectorImpl<SDValue> &Results, SelectionDAG &DAG) const {
  if (N->isStrictFPOpcode())
    report_fatal_error("MMIX constrained floating-point conversions require "
                       "integers of at most 64 bits");
  TargetLowering::ReplaceNodeResults(N, Results, DAG);
}

SDValue MMIXTargetLowering::lowerStrictFP(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0);
  auto Emit = [&](unsigned Opcode, EVT VT, ArrayRef<SDValue> Operands) {
    SmallVector<SDValue, 4> Ops(Operands);
    Ops.push_back(Chain);
    SDNode *N =
        DAG.getMachineNode(Opcode, DL, DAG.getVTList(VT, MVT::Other), Ops);
    N->setFlags(Op->getFlags());
    Chain = SDValue(N, 1);
    return SDValue(N, 0);
  };
  auto Imm = [&](unsigned Value) {
    return DAG.getTargetConstant(Value, DL, MVT::i64);
  };
  auto Extend = [&](SDValue Value) {
    if (Value.getValueType() == MVT::f64)
      return Value;
    // Expand in integer registers so the original NaN kind reaches the
    // exception-raising operation, without a stack spill/reload conversion.
    SDValue Bits = DAG.getNode(MMIXISD::F32_TO_BITS, DL, MVT::i64, Value);
    auto C = [&](uint64_t V) { return DAG.getConstant(V, DL, MVT::i64); };
    auto Bin = [&](unsigned Op, SDValue A, SDValue B) {
      return DAG.getNode(Op, DL, MVT::i64, A, B);
    };
    auto Choose = [&](SDValue Test, uint64_t V, SDValue Yes, SDValue No) {
      SDValue Equal = DAG.getSetCC(DL, MVT::i64, Test, C(V), ISD::SETEQ);
      return DAG.getSelect(DL, MVT::i64, Equal, Yes, No);
    };
    SDValue Sign = Bin(ISD::SHL, Bin(ISD::AND, Bits, C(0x80000000)), C(32));
    SDValue Exponent = Bin(ISD::AND, Bin(ISD::SRL, Bits, C(23)), C(255));
    SDValue Fraction = Bin(ISD::AND, Bits, C(0x7fffff));
    SDValue Normal =
        Bin(ISD::OR,
            Bin(ISD::SHL,
                Choose(Exponent, 255, C(2047), Bin(ISD::ADD, Exponent, C(896))),
                C(52)),
            Bin(ISD::SHL, Fraction, C(29)));
    SDValue Shift =
        Bin(ISD::SUB, DAG.getNode(ISD::CTLZ, DL, MVT::i64, Fraction), C(40));
    SDValue Subnormal = Bin(
        ISD::OR, Bin(ISD::SHL, Bin(ISD::SUB, C(897), Shift), C(52)),
        Bin(ISD::SHL,
            Bin(ISD::AND, Bin(ISD::SHL, Fraction, Shift), C(0x7fffff)), C(29)));
    SDValue Magnitude =
        Choose(Exponent, 0, Choose(Fraction, 0, C(0), Subnormal), Normal);
    Value =
        DAG.getNode(ISD::BITCAST, DL, MVT::f64, Bin(ISD::OR, Sign, Magnitude));
    // Multiplication by one raises invalid for signaling NaNs, without
    // changing finite values or the sign of zero.
    return Emit(MMIX::FMUL, MVT::f64,
                {Value, DAG.getConstantFP(1.0, DL, MVT::f64)});
  };
  auto Round = [&](SDValue Value) {
    SDValue Slot = DAG.CreateStackTemporary(MVT::f32, 4);
    auto PtrInfo = MachinePointerInfo::getFixedStack(
        DAG.getMachineFunction(), cast<FrameIndexSDNode>(Slot)->getIndex());
    // STSF uses rA and supplies the binary32 range and inexact exceptions.
    auto *Store = DAG.getMachineNode(MMIX::STSFI, DL, MVT::Other,
                                    {Value, Slot, Imm(0), Chain});
    Store->setFlags(Op->getFlags());
    auto *MMO = DAG.getMachineFunction().getMachineMemOperand(
        PtrInfo, MachineMemOperand::MOStore, 4, Align(4));
    DAG.setNodeMemRefs(Store, {MMO});
    Chain = SDValue(Store, 0);
    SDValue Bits = DAG.getExtLoad(ISD::ZEXTLOAD, DL, MVT::i64, Chain, Slot,
                                 PtrInfo, MVT::i32, Align(4));
    Chain = Bits.getValue(1);
    return DAG.getNode(MMIXISD::BITS_TO_F32, DL, MVT::f32, Bits);
  };
  auto Finish = [&](SDValue Value) {
    return DAG.getMergeValues({Value, Chain}, DL);
  };

  unsigned Opcode = Op.getOpcode();
  if (Opcode == ISD::STRICT_FP_EXTEND)
    return Finish(Extend(Op.getOperand(1)));
  if (Opcode == ISD::STRICT_FP_ROUND)
    return Finish(Round(Op.getOperand(1)));
  if (Opcode == ISD::STRICT_SINT_TO_FP || Opcode == ISD::STRICT_UINT_TO_FP) {
    bool Signed = Opcode == ISD::STRICT_SINT_TO_FP;
    SDValue Integer = Op.getOperand(1);
    // The wide compiler-rt helpers assume nearest rounding and do not report
    // floating exceptions. Do not silently use them for constrained inputs.
    if (Integer.getValueSizeInBits() > 64)
      report_fatal_error("MMIX constrained floating-point conversions require "
                         "integers of at most 64 bits");
    Integer = DAG.getNode(Signed ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND, DL,
                         MVT::i64, Integer);
    bool Short = Op.getValueType() == MVT::f32;
    unsigned Instr = Short ? (Signed ? MMIX::SFLOT : MMIX::SFLOTU)
                           : (Signed ? MMIX::FLOT : MMIX::FLOTU);
    SDValue Value = Emit(Instr, MVT::f64, {Imm(0), Integer});
    return Finish(Short ? Round(Value) : Value);
  }
  if (Opcode == ISD::STRICT_FP_TO_SINT || Opcode == ISD::STRICT_FP_TO_UINT) {
    SDValue Value = Extend(Op.getOperand(1));
    // LLVM out-of-range conversion results are unspecified. FIXU truncates
    // all defined inputs and raises invalid for NaN/infinity without W traps.
    return Finish(Emit(MMIX::FIXU, MVT::i64, {Imm(1), Value}));
  }
  if (Opcode == ISD::STRICT_FSETCC || Opcode == ISD::STRICT_FSETCCS) {
    SDValue LHS = Extend(Op.getOperand(1));
    SDValue RHS = Extend(Op.getOperand(2));
    if (Opcode == ISD::STRICT_FSETCCS) {
      // FCMP raises invalid for every NaN, including quiet NaNs.
      Emit(MMIX::FCMP, MVT::i64, {LHS, RHS});
    } else {
      SDValue One = DAG.getConstantFP(1.0, DL, MVT::f64);
      LHS = Emit(MMIX::FMUL, MVT::f64, {LHS, One});
      RHS = Emit(MMIX::FMUL, MVT::f64, {RHS, One});
    }
    SDValue Quiet = DAG.getNode(ISD::SETCC, DL, Op.getValueType(), LHS, RHS,
                               Op.getOperand(3));
    return Finish(Quiet);
  }

  SDValue LHS = Extend(Op.getOperand(1));
  SDValue Value;
  unsigned Instr;
  switch (Opcode) {
  case ISD::STRICT_FADD:
    Instr = MMIX::FADD;
    break;
  case ISD::STRICT_FSUB:
    Instr = MMIX::FSUB;
    break;
  case ISD::STRICT_FMUL:
    Instr = MMIX::FMUL;
    break;
  case ISD::STRICT_FDIV:
    Instr = MMIX::FDIV;
    break;
  default:
    Instr = 0;
    break;
  }
  if (Instr) {
    SDValue RHS = Extend(Op.getOperand(2));
    Value = Emit(Instr, MVT::f64, {LHS, RHS});
  } else {
    unsigned Mode;
    switch (Opcode) {
    case ISD::STRICT_FSQRT:
    case ISD::STRICT_FRINT:
      Mode = 0;
      break;
    case ISD::STRICT_FTRUNC:
      Mode = 1;
      break;
    case ISD::STRICT_FCEIL:
      Mode = 2;
      break;
    case ISD::STRICT_FFLOOR:
      Mode = 3;
      break;
    case ISD::STRICT_FROUNDEVEN:
      Mode = 4;
      break;
    default:
      report_fatal_error(
          "unsupported MMIX constrained floating-point operation");
    }
    Value = Emit(Opcode == ISD::STRICT_FSQRT ? MMIX::FSQRT : MMIX::FINT,
                 MVT::f64, {Imm(Mode), LHS});
  }
  // MMIXware section 26 guarantees that a single operation on extended
  // binary32 inputs followed by STSF has no double-rounding error.
  return Finish(Op.getValueType() == MVT::f32 ? Round(Value) : Value);
}
