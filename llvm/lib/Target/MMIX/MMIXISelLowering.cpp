//===-- MMIXISelLowering.cpp - MMIX DAG lowering implementation ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MMIXISelLowering.h"
#include "MCTargetDesc/MMIXBaseInfo.h"
#include "MCTargetDesc/MMIXMCTargetDesc.h"
#include "MMIXAggregateABI.h"
#include "MMIXCallingConv.h"
#include "MMIXMachineFunctionInfo.h"
#include "MMIXSubtarget.h"
#include "MMIXTailCall.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicsMMIX.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <iterator>
#include <limits>
#include <string>

using namespace llvm;

Register MMIXTargetLowering::getExceptionPointerRegister(
    ExceptionHandling EH, const Constant *) const {
  return EH == ExceptionHandling::DwarfCFI ? Register(MMIX::R231) : Register();
}

Register MMIXTargetLowering::getExceptionSelectorRegister(
    ExceptionHandling EH, const Constant *) const {
  return EH == ExceptionHandling::DwarfCFI ? Register(MMIX::R232) : Register();
}

#define GET_CALLING_CONV_IMPL
#include "MMIXGenCallingConv.inc"

#define GET_REGISTER_MATCHER
#include "MMIXGenAsmMatcher.inc"

static bool isMMIXIntegerInlineAsmConstraint(char Constraint) {
  switch (Constraint) {
  case 'I':
  case 'J':
  case 'K':
  case 'M':
  case 'O':
    return true;
  default:
    return false;
  }
}

static bool isMMIXInlineAsmImmediate(char Constraint, int64_t Value) {
  switch (Constraint) {
  case 'I':
    return Value >= 0 && isUInt<8>(uint64_t(Value));
  case 'J':
    return Value >= 0 && isUInt<16>(uint64_t(Value));
  case 'K':
    return Value >= -255 && Value <= 0;
  case 'M':
    return Value == 0;
  case 'O':
    return Value == 3 || Value == 5 || Value == 9 || Value == 17;
  default:
    return false;
  }
}

static bool isMMIXNonlocalControlIntrinsic(Intrinsic::ID ID) {
  switch (ID) {
  case Intrinsic::eh_sjlj_lsda:
  case Intrinsic::eh_sjlj_callsite:
  case Intrinsic::eh_sjlj_functioncontext:
  case Intrinsic::eh_sjlj_setjmp:
  case Intrinsic::eh_sjlj_longjmp:
  case Intrinsic::eh_sjlj_setup_dispatch:
    return true;
  default:
    return false;
  }
}

static std::optional<std::pair<const GlobalAddressSDNode *, int64_t>>
getMMIXDirectGlobalCallee(SDValue Callee) {
  if (const auto *GA = dyn_cast<GlobalAddressSDNode>(Callee))
    return std::pair(GA, GA->getOffset());

  SDValue Symbol;
  const ConstantSDNode *Constant = nullptr;
  if (Callee.getOpcode() == ISD::ADD) {
    if ((Constant = dyn_cast<ConstantSDNode>(Callee.getOperand(1))))
      Symbol = Callee.getOperand(0);
    else if ((Constant = dyn_cast<ConstantSDNode>(Callee.getOperand(0))))
      Symbol = Callee.getOperand(1);
  } else if (Callee.getOpcode() == ISD::SUB) {
    Symbol = Callee.getOperand(0);
    Constant = dyn_cast<ConstantSDNode>(Callee.getOperand(1));
  }

  const auto *GA = dyn_cast_or_null<GlobalAddressSDNode>(Symbol.getNode());
  if (!GA || !Constant)
    return std::nullopt;

  int64_t Addend = Constant->getSExtValue();
  if (Callee.getOpcode() == ISD::SUB && SubOverflow(int64_t(0), Addend, Addend))
    report_fatal_error("MMIX direct call symbol addend is out of range");

  int64_t Offset;
  if (AddOverflow(GA->getOffset(), Addend, Offset))
    report_fatal_error("MMIX direct call symbol addend is out of range");
  return std::pair(GA, Offset);
}

static const Value *getMMIXStructRetArgument(const Function &F) {
  for (const Argument &Arg : F.args())
    if (Arg.hasStructRetAttr())
      return &Arg;
  return nullptr;
}

static const Value *getMMIXStructRetArgument(const CallBase *Call) {
  if (!Call)
    return nullptr;
  for (unsigned I = 0; I != Call->arg_size(); ++I)
    if (Call->paramHasAttr(I, Attribute::StructRet))
      return Call->getArgOperand(I);
  return nullptr;
}

static bool isUnsafeInlineAsmRegister(MCRegister Reg) {
  switch (Reg.id()) {
  case MMIX::R30:  // Holds the incoming rJ in non-leaf functions.
  case MMIX::R253: // Frame pointer.
  case MMIX::R254: // Stack pointer.
  case MMIX::RA:   // Floating environment.
  case MMIX::RG:   // Global-register threshold.
  case MMIX::RJ:   // Return address.
  case MMIX::RL:   // Local-register count.
  case MMIX::RN:   // Register-stack serial number.
  case MMIX::RO:   // Register-stack offset.
  case MMIX::RS:   // Register-stack pointer.
    return true;
  default:
    return false;
  }
}

static StringRef getMMIXModuleOnlyMnemonic(StringRef Token) {
  for (StringRef Mnemonic : {"TRAP", "TRIP", "RESUME", "SAVE", "UNSAVE",
                             "PUSHJ", "PUSHGO", "GO", "POP"})
    if (Token.equals_insensitive(Mnemonic))
      return Mnemonic;
  return {};
}

static StringRef getMMIXModuleOnlyPutRegister(StringRef Token) {
  for (StringRef Register : {"rJ", "rG", "rL", "rA", "rN", "rO", "rS"})
    if (Token.equals_insensitive(Register))
      return Register;
  return {};
}

struct MMIXModuleOnlyInstruction {
  StringRef Mnemonic;
  StringRef Operand;
};

static MMIXModuleOnlyInstruction
findMMIXModuleOnlyInstruction(StringRef AsmString) {
  while (!AsmString.empty()) {
    auto [Line, RemainingLines] = AsmString.split('\n');
    AsmString = RemainingLines;
    Line = Line.split('#').first;

    while (!Line.empty()) {
      auto [Statement, RemainingStatements] = Line.split(';');
      Line = RemainingStatements;
      Statement = Statement.trim();

      // Skip labels so a complete instruction statement is classified by its
      // mnemonic rather than by the surrounding inline-assembly layout.
      while (!Statement.empty()) {
        size_t TokenEnd = Statement.find_first_of(" \t:");
        StringRef Token = Statement.take_front(TokenEnd);
        if (TokenEnd != StringRef::npos && Statement[TokenEnd] == ':') {
          Statement = Statement.drop_front(TokenEnd + 1).ltrim();
          continue;
        }
        if (StringRef Mnemonic = getMMIXModuleOnlyMnemonic(Token);
            !Mnemonic.empty())
          return {Mnemonic, {}};
        if (TokenEnd != StringRef::npos && Token.equals_insensitive("PUT")) {
          StringRef Operands = Statement.drop_front(TokenEnd).ltrim();
          StringRef Register =
              Operands.take_front(Operands.find_first_of(" \t,"));
          if (StringRef RestrictedRegister =
                  getMMIXModuleOnlyPutRegister(Register);
              !RestrictedRegister.empty())
            return {"PUT", RestrictedRegister};
        }
        break;
      }
    }
  }
  return {{}, {}};
}

static MCRegister getMMIXSpecialRegister(unsigned Selector,
                                         const TargetRegisterInfo &TRI) {
  for (MCPhysReg Reg : MMIX::SPR64RegClass)
    if (TRI.getEncodingValue(Reg) == Selector)
      return Reg;
  llvm_unreachable("missing MMIX special-register definition");
}

static bool isMMIXSystemSpecialRegister(MCRegister Reg) {
  switch (Reg.id()) {
  case MMIX::RC:
  case MMIX::RI:
  case MMIX::RT:
  case MMIX::RTT:
  case MMIX::RK:
  case MMIX::RQ:
  case MMIX::RU:
    return true;
  default:
    return false;
  }
}

static SDValue emitMMIXIntrinsicError(SDValue Op, StringRef IntrinsicName,
                                      const Twine &Message, SelectionDAG &DAG) {
  DAG.getContext()->emitError(Twine(IntrinsicName) + ": " + Message);
  if (Op.getOpcode() == ISD::INTRINSIC_W_CHAIN)
    return DAG.getMergeValues(
        {DAG.getUNDEF(Op.getValueType()), Op.getOperand(0)}, SDLoc(Op));
  return Op.getOperand(0);
}

static SDValue lowerMMIXSpecialRegisterIntrinsic(SDValue Op,
                                                 SelectionDAG &DAG) {
  bool IsGet = Op.getOpcode() == ISD::INTRINSIC_W_CHAIN;
  StringRef IntrinsicName = IsGet ? "llvm.mmix.get" : "llvm.mmix.put";
  auto *SelectorNode = dyn_cast<ConstantSDNode>(Op.getOperand(2));
  if (!SelectorNode)
    return emitMMIXIntrinsicError(Op, IntrinsicName,
                                  "selector must be an immediate", DAG);

  uint64_t Selector = SelectorNode->getZExtValue();
  if (!isUInt<5>(Selector))
    return emitMMIXIntrinsicError(Op, IntrinsicName,
                                  "selector must be in the range [0, 31]", DAG);

  const MMIXSubtarget &STI =
      DAG.getMachineFunction().getSubtarget<MMIXSubtarget>();
  const TargetRegisterInfo &TRI = *STI.getRegisterInfo();
  MCRegister Reg = getMMIXSpecialRegister(Selector, TRI);
  SDLoc DL(Op);
  SDValue SpecialReg = DAG.getRegister(Reg, MVT::i64);
  if (IsGet)
    return DAG.getNode(MMIXISD::GET_SPECIAL_REGISTER, DL,
                       {MVT::i64, MVT::Other}, {Op.getOperand(0), SpecialReg});

  std::string RegName = TRI.getName(Reg);
  RegName.front() = 'r';
  switch (Reg.id()) {
  case MMIX::RN:
  case MMIX::RO:
  case MMIX::RS:
    return emitMMIXIntrinsicError(
        Op, IntrinsicName,
        Twine("register '") + RegName + "' is architecturally read-only", DAG);
  case MMIX::RJ:
  case MMIX::RG:
  case MMIX::RL:
    return emitMMIXIntrinsicError(Op, IntrinsicName,
                                  Twine("register '") + RegName +
                                      "' is reserved by the MMIX C ABI",
                                  DAG);
  case MMIX::RA:
    return emitMMIXIntrinsicError(
        Op, IntrinsicName,
        "register 'rA' requires explicit floating-environment modeling", DAG);
  default:
    break;
  }

  if (isMMIXSystemSpecialRegister(Reg) && !STI.hasMMIXSystem())
    return emitMMIXIntrinsicError(Op, IntrinsicName,
                                  Twine("register '") + RegName +
                                      "' requires the system target feature",
                                  DAG);
  if (Reg == MMIX::RV && !STI.hasMMIXVirtualMemory())
    return emitMMIXIntrinsicError(
        Op, IntrinsicName,
        "register 'rV' requires the virtual-memory target feature", DAG);

  return DAG.getNode(MMIXISD::PUT_SPECIAL_REGISTER, DL, MVT::Other,
                     {Op.getOperand(0), SpecialReg, Op.getOperand(3),
                      DAG.getConstant(Selector, DL, MVT::i64)});
}

static SDValue lowerMMIXCacheIntrinsic(SDValue Op, unsigned IntrinsicID,
                                       SelectionDAG &DAG) {
  const MMIXSubtarget &STI =
      DAG.getMachineFunction().getSubtarget<MMIXSubtarget>();
  StringRef IntrinsicName;
  unsigned Operation;
  switch (IntrinsicID) {
  case Intrinsic::mmix_preld:
    IntrinsicName = "llvm.mmix.preld";
    Operation = MMIXISD::CachePreload;
    break;
  case Intrinsic::mmix_prego:
    IntrinsicName = "llvm.mmix.prego";
    Operation = MMIXISD::CachePrefetchForExecution;
    break;
  case Intrinsic::mmix_prest:
    IntrinsicName = "llvm.mmix.prest";
    Operation = MMIXISD::CachePrestore;
    break;
  case Intrinsic::mmix_syncd:
    IntrinsicName = "llvm.mmix.syncd";
    Operation = MMIXISD::CacheSyncData;
    break;
  case Intrinsic::mmix_syncid:
    IntrinsicName = "llvm.mmix.syncid";
    Operation = MMIXISD::CacheSyncInstructionAndData;
    break;
  default:
    llvm_unreachable("not an MMIX cache intrinsic");
  }

  if (!STI.hasMMIXCache())
    return emitMMIXIntrinsicError(Op, IntrinsicName,
                                  "requires the cache target feature", DAG);

  auto *SpanNode = dyn_cast<ConstantSDNode>(Op.getOperand(3));
  if (!SpanNode)
    return emitMMIXIntrinsicError(Op, IntrinsicName,
                                  "span must be an immediate", DAG);
  uint64_t Span = SpanNode->getZExtValue();
  if (!isUInt<8>(Span))
    return emitMMIXIntrinsicError(Op, IntrinsicName,
                                  "span must be in the range [0, 255]", DAG);

  SDLoc DL(Op);
  return DAG.getNode(MMIXISD::CACHE_OPERATION, DL, MVT::Other,
                     {Op.getOperand(0), Op.getOperand(2),
                      DAG.getConstant(Span, DL, MVT::i64),
                      DAG.getConstant(Operation, DL, MVT::i64)});
}

static SDValue lowerMMIXSyncIntrinsic(SDValue Op, SelectionDAG &DAG) {
  const MMIXSubtarget &STI =
      DAG.getMachineFunction().getSubtarget<MMIXSubtarget>();
  if (!STI.hasMMIXSystem())
    return emitMMIXIntrinsicError(Op, "llvm.mmix.sync",
                                  "requires the system target feature", DAG);

  auto *ModeNode = dyn_cast<ConstantSDNode>(Op.getOperand(2));
  if (!ModeNode)
    return emitMMIXIntrinsicError(Op, "llvm.mmix.sync",
                                  "mode must be an immediate", DAG);
  uint64_t Mode = ModeNode->getZExtValue();
  if (!isUInt<3>(Mode))
    return emitMMIXIntrinsicError(Op, "llvm.mmix.sync",
                                  "mode must be in the range [0, 7]", DAG);

  SDLoc DL(Op);
  return DAG.getNode(MMIXISD::SYNC, DL, MVT::Other,
                     {Op.getOperand(0), DAG.getConstant(Mode, DL, MVT::i64)});
}

static SDValue lowerMMIXUncachedMemoryIntrinsic(SDValue Op,
                                                unsigned IntrinsicID,
                                                SelectionDAG &DAG) {
  const MMIXSubtarget &STI =
      DAG.getMachineFunction().getSubtarget<MMIXSubtarget>();
  bool IsLoad = IntrinsicID == Intrinsic::mmix_ldunc;
  StringRef IntrinsicName = IsLoad ? "llvm.mmix.ldunc" : "llvm.mmix.stunc";
  if (!STI.hasMMIXCache())
    return emitMMIXIntrinsicError(Op, IntrinsicName,
                                  "requires the cache target feature", DAG);

  auto *Mem = cast<MemIntrinsicSDNode>(Op);
  SDLoc DL(Op);
  if (IsLoad)
    return DAG.getMemIntrinsicNode(MMIXISD::UNCACHED_LOAD, DL, Op->getVTList(),
                                   {Op.getOperand(0), Op.getOperand(2)},
                                   Mem->getMemoryVT(), Mem->getMemOperand());

  return DAG.getMemIntrinsicNode(
      MMIXISD::UNCACHED_STORE, DL, Op->getVTList(),
      {Op.getOperand(0), Op.getOperand(2), Op.getOperand(3)},
      Mem->getMemoryVT(), Mem->getMemOperand());
}

static SDValue lowerMMIXVirtualTranslationIntrinsic(SDValue Op,
                                                    SelectionDAG &DAG) {
  const MMIXSubtarget &STI =
      DAG.getMachineFunction().getSubtarget<MMIXSubtarget>();
  if (!STI.hasMMIXVirtualMemory())
    return emitMMIXIntrinsicError(Op, "llvm.mmix.ldvts",
                                  "requires the virtual-memory target feature",
                                  DAG);

  return DAG.getNode(MMIXISD::VIRTUAL_TRANSLATION_SEARCH, SDLoc(Op),
                     {MVT::i64, MVT::Other},
                     {Op.getOperand(0), Op.getOperand(2)});
}

static SDValue lowerMMIXTrapIntrinsic(SDValue Op, SelectionDAG &DAG) {
  auto *Service = dyn_cast<ConstantSDNode>(Op.getOperand(3));
  auto *Handle = dyn_cast<ConstantSDNode>(Op.getOperand(4));
  if (!Service || !Handle)
    return emitMMIXIntrinsicError(Op, "llvm.mmix.trap",
                                  "service and handle must be immediate", DAG);
  if (Service->getZExtValue() > 255 || Handle->getZExtValue() > 255)
    return emitMMIXIntrinsicError(
        Op, "llvm.mmix.trap", "service and handle must fit in one byte", DAG);

  SDLoc DL(Op);
  SDValue ServiceImm =
      DAG.getTargetConstant(Service->getZExtValue(), DL, MVT::i64);
  SDValue HandleImm =
      DAG.getTargetConstant(Handle->getZExtValue(), DL, MVT::i64);
  return SDValue(DAG.getMachineNode(MMIX::TRAP_STATE, DL,
                                    {MVT::i64, MVT::Other},
                                    {Op.getOperand(2), ServiceImm, HandleImm,
                                     Op.getOperand(0)}),
                 0);
}

MMIXTargetLowering::AsmOperandInfoVector
MMIXTargetLowering::ParseConstraints(const DataLayout &DL,
                                     const TargetRegisterInfo *TRI,
                                     const CallBase &Call) const {
  // An empty constraint list is valid, not an error marker. Stop on rejection
  // so SelectionDAGBuilder cannot leave consumed asm results without defs.
  const auto *IA = dyn_cast<InlineAsm>(Call.getCalledOperand());
  if (Call.getType()->isVectorTy() ||
      llvm::any_of(Call.args(), [](const Use &Arg) {
        return Arg->getType()->isVectorTy();
      }))
    reportFatalUsageError(
        "MMIX inline assembly does not support vector operands");

  MMIXModuleOnlyInstruction ModuleOnlyInstruction =
      IA ? findMMIXModuleOnlyInstruction(IA->getAsmString())
         : MMIXModuleOnlyInstruction{};
  if (!ModuleOnlyInstruction.Mnemonic.empty()) {
    std::string InstructionName = ModuleOnlyInstruction.Mnemonic.str();
    if (!ModuleOnlyInstruction.Operand.empty()) {
      InstructionName += ' ';
      InstructionName += ModuleOnlyInstruction.Operand;
    }
    reportFatalUsageError(
        Twine("MMIX instruction '") + InstructionName +
        "' is only permitted in module-level inline assembly");
  }

  AsmOperandInfoVector Operands =
      TargetLowering::ParseConstraints(DL, TRI, Call);
  for (AsmOperandInfo &Operand : Operands) {
    bool HadNonOffsettableMemory = false;
    Operand.Codes.erase(std::remove_if(Operand.Codes.begin(),
                                       Operand.Codes.end(),
                                       [&](const std::string &Code) {
                                         if (Code != "V")
                                           return false;
                                         HadNonOffsettableMemory = true;
                                         return true;
                                       }),
                        Operand.Codes.end());
    if (HadNonOffsettableMemory && Operand.Codes.empty()) {
      reportFatalUsageError(
          "MMIX has no non-offsettable inline assembly memory operand");
    }

    const auto *PointerTy =
        Operand.CallOperandVal
            ? dyn_cast<PointerType>(Operand.CallOperandVal->getType())
            : nullptr;
    if (PointerTy && PointerTy->getAddressSpace() != 0 &&
        std::any_of(Operand.Codes.begin(), Operand.Codes.end(),
                    [](const std::string &Code) {
                      return Code == "m" || Code == "o" || Code == "p";
                    })) {
      reportFatalUsageError(
          "MMIX inline assembly does not support memory or address "
          "operands in nonzero address spaces");
    }

    if (Operand.Type != InlineAsm::isClobber)
      continue;
    for (StringRef Code : Operand.Codes) {
      if (!Code.starts_with('{') || !Code.ends_with('}'))
        continue;
      StringRef Name = Code.drop_front().drop_back();
      MCRegister Reg = MatchRegisterName(Name);
      if (!Reg || !isUnsafeInlineAsmRegister(Reg))
        continue;
      reportFatalUsageError(
          Twine("MMIX inline assembly may not clobber register '") +
          Name + "' in an ordinary function");
    }
  }
  return Operands;
}

MMIXTargetLowering::ConstraintType
MMIXTargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    if (isMMIXIntegerInlineAsmConstraint(Constraint[0]))
      return C_Immediate;
    switch (Constraint[0]) {
    case 'G':
      return C_Other;
    case 'm':
    case 'o':
    case 'V':
      return C_Memory;
    case 'p':
      return C_Address;
    default:
      break;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

InlineAsm::ConstraintCode
MMIXTargetLowering::getInlineAsmMemConstraint(StringRef ConstraintCode) const {
  // InlineAsm has no uppercase V code. Its lowercase v code carries the same
  // non-offsettable-memory semantics for target selectors.
  if (ConstraintCode == "V")
    return InlineAsm::ConstraintCode::v;
  return TargetLowering::getInlineAsmMemConstraint(ConstraintCode);
}

MMIXTargetLowering::ConstraintWeight
MMIXTargetLowering::getSingleConstraintMatchWeight(
    AsmOperandInfo &Info, const char *Constraint) const {
  if (!Info.CallOperandVal)
    return CW_Default;

  if (Constraint[1] != '\0')
    return TargetLowering::getSingleConstraintMatchWeight(Info, Constraint);

  if (Constraint[0] == 'r') {
    Type *Ty = Info.CallOperandVal->getType();
    return Ty->isIntegerTy() || Ty->isPointerTy() || Ty->isDoubleTy()
               ? CW_Register
               : CW_Invalid;
  }

  if (Constraint[0] == 'G') {
    const auto *C = dyn_cast<ConstantFP>(Info.CallOperandVal);
    return C && C->isZero() ? CW_Constant : CW_Invalid;
  }

  const auto *C = dyn_cast<ConstantInt>(Info.CallOperandVal);
  if (!C)
    return TargetLowering::getSingleConstraintMatchWeight(Info, Constraint);

  if (!C->getValue().isSignedIntN(64))
    return CW_Invalid;
  if (!isMMIXIntegerInlineAsmConstraint(Constraint[0]))
    return TargetLowering::getSingleConstraintMatchWeight(Info, Constraint);
  return isMMIXInlineAsmImmediate(Constraint[0], C->getSExtValue())
             ? CW_Constant
             : CW_Invalid;
}

std::pair<unsigned, const TargetRegisterClass *>
MMIXTargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                 StringRef Constraint,
                                                 MVT VT) const {
  if (Constraint == "r") {
    if (VT == MVT::f64)
      return {0, &MMIX::FPR64CodeGenRegClass};
    if (!VT.isVector())
      return {0, &MMIX::GPR64CodeGenRegClass};
    return {0, nullptr};
  }

  if (!Constraint.starts_with('{') || !Constraint.ends_with('}'))
    return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);

  MCRegister Reg = MatchRegisterName(Constraint.drop_front().drop_back());
  if (!Reg || isUnsafeInlineAsmRegister(Reg))
    return {0, nullptr};

  if (MMIX::GPR64CodeGenRegClass.contains(Reg)) {
    const TargetRegisterClass *RC = VT == MVT::f64
                                        ? &MMIX::FPR64CodeGenRegClass
                                        : &MMIX::GPR64CodeGenRegClass;
    return {Reg.id(), RC};
  }

  // Reserved architectural and special registers are valid clobber names, but
  // cannot carry values without changing the reviewed register-allocation or
  // special-register state contract.
  if (VT != MVT::Other)
    return {0, nullptr};
  if (MMIX::GPR64RegClass.contains(Reg))
    return {Reg.id(), &MMIX::GPR64RegClass};
  if (MMIX::SPR64RegClass.contains(Reg))
    return {Reg.id(), &MMIX::SPR64RegClass};
  return {0, nullptr};
}

void MMIXTargetLowering::LowerAsmOperandForConstraint(SDValue Op,
                                                      StringRef Constraint,
                                                      std::vector<SDValue> &Ops,
                                                      SelectionDAG &DAG) const {
  if (Constraint.size() != 1)
    return TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops,
                                                        DAG);

  if (Constraint[0] == 'G') {
    if (const auto *C = dyn_cast<ConstantFPSDNode>(Op); C && C->isZero())
      Ops.push_back(DAG.getTargetConstant(0, SDLoc(Op), MVT::i64));
    return;
  }

  const auto *C = dyn_cast<ConstantSDNode>(Op);
  if (!C)
    return TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops,
                                                        DAG);

  if (!isMMIXIntegerInlineAsmConstraint(Constraint[0]))
    return TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops,
                                                        DAG);

  int64_t Value = C->getSExtValue();
  if (isMMIXInlineAsmImmediate(Constraint[0], Value))
    Ops.push_back(DAG.getSignedTargetConstant(Value, SDLoc(Op), MVT::i64));
}

MMIXTargetLowering::MMIXTargetLowering(const TargetMachine &TM,
                                       const MMIXSubtarget &STI)
    : TargetLowering(TM, STI) {
  addRegisterClass(MVT::i64, &MMIX::GPR64CodeGenRegClass);
  addRegisterClass(MVT::f32, &MMIX::F32BitsCodeGenRegClass);
  addRegisterClass(MVT::f64, &MMIX::FPR64CodeGenRegClass);
  computeRegisterProperties(STI.getRegisterInfo());

  setStackPointerRegisterToSaveRestore(MMIX::R254);
  setBooleanContents(ZeroOrOneBooleanContent);
  setBooleanVectorContents(ZeroOrOneBooleanContent);
  setMinFunctionAlignment(Align(4));
  setPrefFunctionAlignment(Align(4));
  setMaxAtomicSizeInBitsSupported(64);
  setMinCmpXchgSizeInBits(64);
  setMinimumJumpTableEntries(std::numeric_limits<unsigned>::max());
  setTargetDAGCombine(ISD::STORE);

  // Keep small object operations inline when generic lowering needs at most
  // eight stores, or four when optimizing for size. Larger and dynamic-sized
  // operations use the C helpers selected by MMIXSubtarget.
  MaxStoresPerMemset = MaxStoresPerMemcpy = MaxStoresPerMemmove = 8;
  MaxStoresPerMemsetOptSize = MaxStoresPerMemcpyOptSize =
      MaxStoresPerMemmoveOptSize = 4;

  auto RejectOperation = [this](unsigned Opcode, MVT VT) {
    setOperationAction(Opcode, VT, Custom);
  };

  // i64 and f64 are native register values. Type legalization promotes narrow
  // integers and splits wider scalar and vector values. f32 registers contain
  // raw low-tetra bits, and supported f32 operations use the explicit
  // promotion actions below. MMIX has no rotate or byte-swap instructions, so
  // expand those i64 operations into supported shifts and Boolean operations.
  setOperationAction(ISD::ROTL, MVT::i64, Expand);
  setOperationAction(ISD::ROTR, MVT::i64, Expand);
  for (unsigned Opcode : {ISD::SHL_PARTS, ISD::SRL_PARTS, ISD::SRA_PARTS})
    setOperationAction(Opcode, MVT::i64, Expand);
  setOperationAction(ISD::BSWAP, MVT::i64, Expand);
  setOperationAction(ISD::CTLZ, MVT::i64, Expand);
  setOperationAction(ISD::CTTZ, MVT::i64, Expand);
  setOperationAction(ISD::CTPOP, MVT::i64, Legal);
  setOperationAction(ISD::USUBSAT, MVT::i64, Legal);

  setOperationAction(ISD::SETCC, MVT::i64, Legal);
  setOperationAction(ISD::SELECT, MVT::i64, Legal);
  setOperationAction(ISD::SELECT_CC, MVT::i64, Expand);

  setOperationAction(ISD::MULHU, MVT::i64, Expand);
  setOperationAction(ISD::MULHS, MVT::i64, Expand);
  setOperationAction(ISD::UMUL_LOHI, MVT::i64, Custom);
  setOperationAction(ISD::SMUL_LOHI, MVT::i64, Custom);
  setOperationAction(ISD::UMULO, MVT::i64, Expand);
  setOperationAction(ISD::SMULO, MVT::i64, Expand);

  // MMIX's divide-by-zero and INT_MIN/-1 exceptional cases correspond to
  // poison-producing LLVM inputs. All defined signed inputs need the quotient
  // and remainder correction implemented by the custom SDIVREM lowering.
  for (unsigned Opcode : {ISD::SDIV, ISD::UDIV, ISD::SREM, ISD::UREM})
    setOperationAction(Opcode, MVT::i64, Expand);
  setOperationAction(ISD::SDIVREM, MVT::i64, Custom);
  setOperationAction(ISD::UDIVREM, MVT::i64, Custom);

  // Overflow-producing nodes must be expanded rather than selected as MMIX's
  // trapping signed arithmetic instructions. LLVM's nsw flag is poison
  // semantics and likewise does not authorize a hardware trap.
  for (unsigned Opcode : {ISD::SADDO, ISD::UADDO, ISD::SSUBO, ISD::USUBO})
    setOperationAction(Opcode, MVT::i64, Expand);

  for (unsigned Opcode :
       {ISD::FADD, ISD::FSUB, ISD::FMUL, ISD::FDIV, ISD::FSQRT})
    setOperationAction(Opcode, MVT::f64, Legal);
  setOperationPromotedToType(
      {ISD::FADD, ISD::FSUB, ISD::FMUL, ISD::FDIV, ISD::FSQRT}, MVT::f32,
      MVT::f64);
  setOperationAction(ISD::ConstantFP, MVT::f64, Legal);
  setOperationAction(ISD::SETCC, MVT::f64, Custom);
  setOperationPromotedToType(ISD::SETCC, MVT::f32, MVT::f64);

  for (unsigned Opcode : {ISD::SINT_TO_FP, ISD::UINT_TO_FP})
    setOperationAction(Opcode, MVT::i64, Custom);
  for (unsigned Opcode : {ISD::FP_TO_SINT, ISD::FP_TO_UINT})
    setOperationAction(Opcode, MVT::i64, Custom);
  static constexpr unsigned FloatingIntegralOperations[] = {
      ISD::FTRUNC, ISD::FCEIL, ISD::FFLOOR, ISD::FROUNDEVEN, ISD::FRINT};
  for (unsigned Opcode : FloatingIntegralOperations)
    setOperationAction(Opcode, MVT::f64, Legal);
  setOperationPromotedToType(FloatingIntegralOperations, MVT::f32, MVT::f64);

  for (unsigned Opcode : {ISD::FNEG, ISD::FABS, ISD::FCOPYSIGN})
    setOperationAction(Opcode, MVT::f64, Expand);
  setOperationPromotedToType({ISD::FNEG, ISD::FABS, ISD::FCOPYSIGN},
                             MVT::f32, MVT::f64);
  // Keep minnum/maxnum independent of libm and preserve signed-zero ordering.
  for (unsigned Opcode : {ISD::FMINNUM, ISD::FMAXNUM})
    setOperationAction(Opcode, {MVT::f32, MVT::f64}, Custom);
  setOperationAction(ISD::SELECT, MVT::f64, Custom);
  setOperationPromotedToType(ISD::SELECT, MVT::f32, MVT::f64);
  setOperationAction(ISD::SELECT_CC, MVT::f32, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::f64, Expand);

  static constexpr unsigned FloatingLibcallOperations[] = {
      ISD::FREM, ISD::FMA, ISD::FROUND, ISD::FNEARBYINT};
  for (unsigned Opcode : FloatingLibcallOperations)
    setOperationAction(Opcode, {MVT::f32, MVT::f64}, LibCall);

  static constexpr unsigned StrictFloatingOperations[] = {
      ISD::STRICT_FADD,       ISD::STRICT_FSUB,       ISD::STRICT_FMUL,
      ISD::STRICT_FDIV,       ISD::STRICT_FREM,       ISD::STRICT_FSQRT,
      ISD::STRICT_FMA,        ISD::STRICT_FSETCC,     ISD::STRICT_FSETCCS,
      ISD::STRICT_FP_ROUND,   ISD::STRICT_FP_EXTEND,  ISD::STRICT_FTRUNC,
      ISD::STRICT_FCEIL,      ISD::STRICT_FFLOOR,     ISD::STRICT_FROUND,
      ISD::STRICT_FROUNDEVEN, ISD::STRICT_FNEARBYINT, ISD::STRICT_FRINT};
  for (unsigned Opcode : StrictFloatingOperations)
    RejectOperation(Opcode, MVT::f64);
  for (unsigned Opcode : StrictFloatingOperations)
    RejectOperation(Opcode, MVT::f32);
  for (unsigned Opcode : {ISD::STRICT_SINT_TO_FP, ISD::STRICT_UINT_TO_FP,
                          ISD::STRICT_FP_TO_SINT, ISD::STRICT_FP_TO_UINT})
    RejectOperation(Opcode, MVT::i64);

  static constexpr unsigned SymbolicAddressOperations[] = {
      ISD::GlobalAddress, ISD::ExternalSymbol, ISD::BlockAddress,
      ISD::ConstantPool, ISD::JumpTable, ISD::GlobalTLSAddress};
  for (unsigned Opcode : SymbolicAddressOperations)
    setOperationAction(Opcode, MVT::i64, Custom);

  static constexpr unsigned AddressOperations[] = {
      ISD::FRAMEADDR, ISD::ADDRSPACECAST};
  for (unsigned Opcode : AddressOperations)
    RejectOperation(Opcode, MVT::i64);
  setOperationAction(ISD::RETURNADDR, MVT::i64, Custom);

  setOperationAction(ISD::FrameIndex, MVT::i64, Legal);
  setOperationAction(ISD::LOAD, MVT::i64, Legal);
  setOperationAction(ISD::STORE, MVT::i64, Legal);
  for (ISD::LoadExtType ExtType :
       {ISD::EXTLOAD, ISD::SEXTLOAD, ISD::ZEXTLOAD})
    setLoadExtAction(ExtType, MVT::i64, MVT::i1, Promote);
  for (MVT MemVT : {MVT::i8, MVT::i16, MVT::i32}) {
    setLoadExtAction(ISD::EXTLOAD, MVT::i64, MemVT, Legal);
    setLoadExtAction(ISD::SEXTLOAD, MVT::i64, MemVT, Legal);
    setLoadExtAction(ISD::ZEXTLOAD, MVT::i64, MemVT, Legal);
    setTruncStoreAction(MVT::i64, MemVT, Legal);
  }
  setOperationAction(ISD::LOAD, MVT::f64, Legal);
  setOperationAction(ISD::STORE, MVT::f64, Legal);
  setOperationAction(ISD::LOAD, MVT::f32, Custom);
  setOperationAction(ISD::STORE, MVT::f32, Custom);
  setOperationAction(ISD::FP_EXTEND, MVT::f64, Custom);
  setOperationAction(ISD::FP_ROUND, MVT::f32, Custom);
  RejectOperation(ISD::FP16_TO_FP, MVT::f32);
  RejectOperation(ISD::FP_TO_FP16, MVT::f32);
  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f32, Legal);
  setTruncStoreAction(MVT::f64, MVT::f32, Legal);
  setOperationAction(ISD::BR_CC, MVT::i64, Expand);
  setOperationAction(ISD::BR_CC, MVT::f32, Expand);
  setOperationAction(ISD::BR_CC, MVT::f64, Expand);
  setOperationAction(ISD::BRCOND, MVT::Other, Legal);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  RejectOperation(ISD::BRIND, MVT::Other);
  setOperationAction(ISD::TRAP, MVT::Other, Legal);
  setOperationAction(ISD::ATOMIC_CMP_SWAP, MVT::i64, Legal);
  setOperationAction(ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS, MVT::i64, Expand);
  setOperationAction(ISD::ATOMIC_FENCE, MVT::Other, Custom);
  setOperationAction(ISD::INTRINSIC_W_CHAIN, MVT::i64, Custom);
  setOperationAction(ISD::INTRINSIC_W_CHAIN, MVT::Other, Custom);
  setOperationAction(ISD::INTRINSIC_VOID, MVT::Other, Custom);
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VACOPY, MVT::Other, Expand);
  setOperationAction(ISD::VAEND, MVT::Other, Expand);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i64, Expand);
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);
}

bool MMIXTargetLowering::allowsMisalignedMemoryAccesses(
    EVT, unsigned, Align, MachineMemOperand::Flags, unsigned *) const {
  // MMIX rounds a misaligned multi-byte address down instead of performing
  // the byte sequence required by LLVM semantics. Let SelectionDAG expand it.
  return false;
}

TargetLowering::AtomicExpansionKind
MMIXTargetLowering::shouldExpandAtomicLoadInIR(LoadInst *LI) const {
  if (LI->getType()->isIntegerTy() &&
      LI->getType()->getPrimitiveSizeInBits() < 64)
    return AtomicExpansionKind::CustomExpand;
  return AtomicExpansionKind::CmpXChg;
}

Instruction *MMIXTargetLowering::emitLeadingFence(IRBuilderBase &Builder,
                                                  Instruction *Inst,
                                                  AtomicOrdering Ord) const {
  if (Ord == AtomicOrdering::SequentiallyConsistent)
    return Builder.CreateFence(Ord);
  return TargetLowering::emitLeadingFence(Builder, Inst, Ord);
}

void MMIXTargetLowering::emitExpandAtomicLoad(LoadInst *LI) const {
  auto *ValueTy = cast<IntegerType>(LI->getType());
  unsigned Width = ValueTy->getBitWidth();
  assert((Width == 8 || Width == 16 || Width == 32) &&
         "unexpected narrow MMIX atomic load type");

  IRBuilder<> Builder(LI);
  Type *I64Ty = Builder.getInt64Ty();
  Value *Address = LI->getPointerOperand();
  Value *AddressInt = Builder.CreatePtrToInt(Address, I64Ty);
  Value *ByteOffset =
      Builder.CreateAnd(AddressInt, Builder.getInt64(7), "atomic.byte.offset");
  Value *AlignedInt = Builder.CreateAnd(
      AddressInt, Builder.getInt64(uint64_t(-8)), "atomic.aligned.address");
  Value *AlignedAddress =
      Builder.CreateIntToPtr(AlignedInt, Address->getType());

  unsigned WidthBytes = Width / 8;
  Value *BigEndianByte = Builder.CreateSub(
      Builder.getInt64(8 - WidthBytes), ByteOffset, "atomic.big-endian.byte");
  Value *Shift = Builder.CreateShl(BigEndianByte, 3, "atomic.shift");

  AtomicOrdering Order = LI->getOrdering();
  if (Order == AtomicOrdering::Unordered)
    Order = AtomicOrdering::Monotonic;
  Constant *Zero = Builder.getInt64(0);
  auto *Pair = Builder.CreateAtomicCmpXchg(
      AlignedAddress, Zero, Zero, Align(8), Order,
      AtomicCmpXchgInst::getStrongestFailureOrdering(Order),
      LI->getSyncScopeID());
  Pair->setVolatile(LI->isVolatile());
  Pair->copyMetadata(*LI, {LLVMContext::MD_dbg, LLVMContext::MD_tbaa,
                           LLVMContext::MD_tbaa_struct,
                           LLVMContext::MD_alias_scope, LLVMContext::MD_noalias,
                           LLVMContext::MD_noalias_addrspace,
                           LLVMContext::MD_access_group, LLVMContext::MD_mmra});

  Value *Loaded = Builder.CreateExtractValue(Pair, 0, "atomic.loaded.octa");
  Value *Shifted = Builder.CreateLShr(Loaded, Shift, "atomic.loaded.shifted");
  Value *Result = Builder.CreateTrunc(Shifted, ValueTy, "atomic.loaded");
  LI->replaceAllUsesWith(Result);
  LI->eraseFromParent();
}

void MMIXTargetLowering::getTgtMemIntrinsic(
    SmallVectorImpl<IntrinsicInfo> &Infos, const CallBase &I, MachineFunction &,
    unsigned IntrinsicID) const {
  IntrinsicInfo Info;
  switch (IntrinsicID) {
  case Intrinsic::mmix_ldunc:
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.flags = MachineMemOperand::MOLoad;
    break;
  case Intrinsic::mmix_stunc:
    Info.opc = ISD::INTRINSIC_VOID;
    Info.flags = MachineMemOperand::MOStore;
    break;
  default:
    return;
  }

  Info.memVT = MVT::i64;
  Info.ptrVal = I.getArgOperand(0);
  Info.size = 8;
  Info.align = Align(8);
  Infos.push_back(Info);
}

SDValue MMIXTargetLowering::PerformDAGCombine(SDNode *N,
                                              DAGCombinerInfo &DCI) const {
  auto *Store = dyn_cast<StoreSDNode>(N);
  if (!Store || Store->isTruncatingStore() || Store->isIndexed() ||
      Store->getMemoryVT() != MVT::f32)
    return SDValue();

  SDValue Value = Store->getValue();
  unsigned ConvertOpcode = Value.getOpcode();
  if (ConvertOpcode != ISD::SINT_TO_FP && ConvertOpcode != ISD::UINT_TO_FP)
    return SDValue();

  SDValue Integer = Value.getOperand(0);
  EVT IntegerVT = Integer.getValueType();
  if (!IntegerVT.isInteger() || IntegerVT.isVector() ||
      IntegerVT.getSizeInBits() > 64)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  if (IntegerVT != MVT::i64) {
    unsigned ExtendOpcode =
        ConvertOpcode == ISD::SINT_TO_FP ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
    Integer = DAG.getNode(ExtendOpcode, SDLoc(Integer), MVT::i64, Integer);
  }
  unsigned Opcode =
      ConvertOpcode == ISD::SINT_TO_FP ? MMIXISD::SFLOT : MMIXISD::SFLOTU;
  SDValue Rounded = DAG.getNode(Opcode, SDLoc(Value), MVT::f64, Integer);
  return DAG.getTruncStore(Store->getChain(), SDLoc(Store), Rounded,
                           Store->getBasePtr(), MVT::f32,
                           Store->getMemOperand());
}

SDValue MMIXTargetLowering::LowerOperation(SDValue Op,
                                           SelectionDAG &DAG) const {
  const Function &F = DAG.getMachineFunction().getFunction();
  const SDLoc DL(Op);
  if (Op.getOpcode() == ISD::RETURNADDR) {
    if (Op.getConstantOperandVal(0) != 0)
      reportFatalUsageError(
          Twine("MMIX supports only return address depth 0 in function '") +
          F.getName() + "'");
    MachineFunction &MF = DAG.getMachineFunction();
    MF.getFrameInfo().setReturnAddressIsTaken(true);
    Register Reg = MF.getInfo<MMIXMachineFunctionInfo>()
                       ->getOrCreateReturnAddressRegister(MF);
    return DAG.getCopyFromReg(DAG.getEntryNode(), DL, Reg, MVT::i64);
  }
  if (Op.getOpcode() == ISD::ATOMIC_FENCE) {
    auto SSID = static_cast<SyncScope::ID>(Op.getConstantOperandVal(2));
    if (SSID == SyncScope::SingleThread)
      return DAG.getNode(ISD::MEMBARRIER, DL, MVT::Other, Op.getOperand(0));
    return DAG.getNode(MMIXISD::SYNC, DL, MVT::Other,
                       {Op.getOperand(0), DAG.getConstant(3, DL, MVT::i64)});
  }
  if (Op.getOpcode() == ISD::LOAD && Op.getValueType() == MVT::f32) {
    auto *Load = cast<LoadSDNode>(Op);
    SDValue Bits = DAG.getExtLoad(
        ISD::ZEXTLOAD, DL, MVT::i64, Load->getChain(), Load->getBasePtr(),
        Load->getPointerInfo(), MVT::i32, Load->getAlign(),
        Load->getMemOperand()->getFlags(), Load->getAAInfo());
    SDValue Value = DAG.getNode(MMIXISD::BITS_TO_F32, DL, MVT::f32, Bits);
    return DAG.getMergeValues({Value, Bits.getValue(1)}, DL);
  }
  if (Op.getOpcode() == ISD::STORE) {
    auto *Store = cast<StoreSDNode>(Op);
    if (Store->getMemoryVT() == MVT::f32 && !Store->isTruncatingStore()) {
      SDValue Bits =
          DAG.getNode(MMIXISD::F32_TO_BITS, DL, MVT::i64, Store->getValue());
      return DAG.getTruncStore(Store->getChain(), DL, Bits, Store->getBasePtr(),
                               MVT::i32, Store->getMemOperand());
    }
  }
  if (Op.getOpcode() == ISD::VASTART) {
    MachineFunction &MF = DAG.getMachineFunction();
    const auto *MMFI = MF.getInfo<MMIXMachineFunctionInfo>();
    if (!MMFI->hasVarArgsFrameIndex())
      report_fatal_error("MMIX va_start used outside a variadic function");
    SDValue FI = DAG.getFrameIndex(MMFI->getVarArgsFrameIndex(),
                                   getPointerTy(MF.getDataLayout()));
    const Value *SourceValue =
        cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
    return DAG.getStore(Op.getOperand(0), DL, FI, Op.getOperand(1),
                        MachinePointerInfo(SourceValue));
  }
  if (Op.getOpcode() == ISD::FP_EXTEND &&
      Op.getOperand(0).getValueType() == MVT::f32) {
    MachineFunction &MF = DAG.getMachineFunction();
    SDValue Slot = DAG.CreateStackTemporary(MVT::f32, 4);
    auto PtrInfo = MachinePointerInfo::getFixedStack(
        MF, cast<FrameIndexSDNode>(Slot)->getIndex());
    SDValue Bits =
        DAG.getNode(MMIXISD::F32_TO_BITS, DL, MVT::i64, Op.getOperand(0));
    SDValue Chain = DAG.getTruncStore(DAG.getEntryNode(), DL, Bits, Slot,
                                      PtrInfo, MVT::i32, Align(4));
    return DAG.getExtLoad(ISD::EXTLOAD, DL, MVT::f64, Chain, Slot, PtrInfo,
                          MVT::f32, Align(4));
  }
  if (Op.getOpcode() == ISD::FP_ROUND && Op.getValueType() == MVT::f32) {
    MachineFunction &MF = DAG.getMachineFunction();
    SDValue Slot = DAG.CreateStackTemporary(MVT::f32, 4);
    auto PtrInfo = MachinePointerInfo::getFixedStack(
        MF, cast<FrameIndexSDNode>(Slot)->getIndex());
    SDValue Chain = DAG.getTruncStore(DAG.getEntryNode(), DL, Op.getOperand(0),
                                      Slot, PtrInfo, MVT::f32, Align(4));
    SDValue Bits = DAG.getExtLoad(ISD::ZEXTLOAD, DL, MVT::i64, Chain, Slot,
                                  PtrInfo, MVT::i32, Align(4));
    return DAG.getNode(MMIXISD::BITS_TO_F32, DL, MVT::f32, Bits);
  }
  if (Op.getOpcode() == ISD::SINT_TO_FP ||
      Op.getOpcode() == ISD::UINT_TO_FP) {
    SDValue Integer = Op.getOperand(0);
    EVT IntegerVT = Integer.getValueType();
    if (IntegerVT != MVT::i64) {
      unsigned ExtendOpcode = Op.getOpcode() == ISD::SINT_TO_FP
                                  ? ISD::SIGN_EXTEND
                                  : ISD::ZERO_EXTEND;
      Integer = DAG.getNode(ExtendOpcode, DL, MVT::i64, Integer);
    }
    bool IsSigned = Op.getOpcode() == ISD::SINT_TO_FP;
    if (Op.getValueType() == MVT::f64)
      return DAG.getNode(IsSigned ? MMIXISD::FLOT : MMIXISD::FLOTU, DL,
                         MVT::f64, Integer);

    assert(Op.getValueType() == MVT::f32 &&
           "unexpected MMIX integer-to-floating result type");
    unsigned Opcode = IsSigned ? MMIXISD::SFLOT : MMIXISD::SFLOTU;
    SDValue Rounded = DAG.getNode(Opcode, DL, MVT::f64, Integer);
    return DAG.getNode(ISD::FP_ROUND, DL, MVT::f32, Rounded,
                       DAG.getIntPtrConstant(1, DL, /*isTarget=*/true));
  }
  if (Op.getOpcode() == ISD::FP_TO_SINT ||
      Op.getOpcode() == ISD::FP_TO_UINT) {
    SDValue Floating = Op.getOperand(0);
    if (Floating.getValueType() == MVT::f32)
      Floating = DAG.getNode(ISD::FP_EXTEND, DL, MVT::f64, Floating);
    assert(Floating.getValueType() == MVT::f64 &&
           "unexpected MMIX floating-to-integer source type");
    return DAG.getNode(MMIXISD::FIXU, DL, MVT::i64, Floating);
  }
  if (Op.getOpcode() == ISD::FP16_TO_FP || Op.getOpcode() == ISD::FP_TO_FP16)
    report_fatal_error("unsupported library call operation");
  if (Op.getOpcode() == ISD::ADDRSPACECAST)
    reportFatalUsageError(
        Twine("MMIX does not support nonzero address spaces in function '") +
        F.getName() + "'");
  if (Op.getOpcode() == ISD::GlobalTLSAddress)
    reportFatalUsageError(
        Twine("MMIX does not support thread-local storage in function '") +
        F.getName() + "'");
  if (Op.getOpcode() == ISD::BRIND)
    reportFatalUsageError(
        Twine("MMIX does not support indirect branches in ordinary function '") +
        F.getName() + "'");
  if (Op->isStrictFPOpcode())
    report_fatal_error(
        "MMIX constrained floating-point lowering is not implemented");

  switch (Op.getOpcode()) {
  case ISD::INTRINSIC_W_CHAIN:
  case ISD::INTRINSIC_VOID:
  case ISD::GlobalAddress:
  case ISD::ExternalSymbol:
  case ISD::BlockAddress:
  case ISD::ConstantPool:
  case ISD::JumpTable:
  case ISD::UMUL_LOHI:
  case ISD::SMUL_LOHI:
  case ISD::UDIVREM:
  case ISD::SDIVREM:
  case ISD::SETCC:
  case ISD::SELECT:
  case ISD::FMINNUM:
  case ISD::FMAXNUM:
    break;
  default:
    report_fatal_error(
        Twine("MMIX SelectionDAG operation is not implemented by this "
              "lowering stage: ") +
        Op->getOperationName(&DAG));
  }

  if (Op.getOpcode() == ISD::INTRINSIC_W_CHAIN ||
      Op.getOpcode() == ISD::INTRINSIC_VOID) {
    unsigned IntrinsicID = Op.getConstantOperandVal(1);
    if (isMMIXNonlocalControlIntrinsic(static_cast<Intrinsic::ID>(IntrinsicID)))
      reportFatalUsageError(
          Twine("MMIX does not support nonlocal control transfer in ") +
          "function '" + F.getName() + "'");
    switch (IntrinsicID) {
    case Intrinsic::mmix_get:
    case Intrinsic::mmix_put:
      return lowerMMIXSpecialRegisterIntrinsic(Op, DAG);
    case Intrinsic::mmix_preld:
    case Intrinsic::mmix_prego:
    case Intrinsic::mmix_prest:
    case Intrinsic::mmix_syncd:
    case Intrinsic::mmix_syncid:
      return lowerMMIXCacheIntrinsic(Op, IntrinsicID, DAG);
    case Intrinsic::mmix_sync:
      return lowerMMIXSyncIntrinsic(Op, DAG);
    case Intrinsic::mmix_ldunc:
    case Intrinsic::mmix_stunc:
      return lowerMMIXUncachedMemoryIntrinsic(Op, IntrinsicID, DAG);
    case Intrinsic::mmix_ldvts:
      return lowerMMIXVirtualTranslationIntrinsic(Op, DAG);
    case Intrinsic::mmix_trap:
      return lowerMMIXTrapIntrinsic(Op, DAG);
    default:
      report_fatal_error("unsupported chained MMIX intrinsic");
    }
  }

  if (Op.getOpcode() == ISD::SELECT) {
    SDValue TrueBits =
        DAG.getNode(ISD::BITCAST, DL, MVT::i64, Op.getOperand(1));
    SDValue FalseBits =
        DAG.getNode(ISD::BITCAST, DL, MVT::i64, Op.getOperand(2));
    SDValue Selected = DAG.getNode(ISD::SELECT, DL, MVT::i64, Op.getOperand(0),
                                   TrueBits, FalseBits);
    return DAG.getNode(ISD::BITCAST, DL, MVT::f64, Selected);
  }

  if (Op.getOpcode() == ISD::FMINNUM || Op.getOpcode() == ISD::FMAXNUM) {
    const bool IsMax = Op.getOpcode() == ISD::FMAXNUM;
    EVT VT = Op.getValueType();
    EVT BitsVT = MVT::i64;
    SDValue LHS = Op.getOperand(0);
    SDValue RHS = Op.getOperand(1);
    SDValue LHSBits =
        VT == MVT::f32
            ? DAG.getNode(MMIXISD::F32_TO_BITS, DL, MVT::i64, LHS)
            : DAG.getNode(ISD::BITCAST, DL, MVT::i64, LHS);
    SDValue RHSBits =
        VT == MVT::f32
            ? DAG.getNode(MMIXISD::F32_TO_BITS, DL, MVT::i64, RHS)
            : DAG.getNode(ISD::BITCAST, DL, MVT::i64, RHS);
    SDValue Zero = DAG.getConstant(0, DL, BitsVT);
    SDValue One = DAG.getConstant(1, DL, MVT::i64);
    // Raw-bit classification preserves NaN kind and signed-zero ordering.
    const uint64_t MagnitudeMaskValue =
        VT == MVT::f32 ? UINT64_C(0x7fffffff)
                       : UINT64_C(0x7fffffffffffffff);
    const uint64_t ExponentMaskValue =
        VT == MVT::f32 ? UINT64_C(0x7f800000)
                       : UINT64_C(0x7ff0000000000000);
    const uint64_t FractionMaskValue =
        VT == MVT::f32 ? UINT64_C(0x007fffff)
                       : UINT64_C(0x000fffffffffffff);
    const uint64_t QuietMaskValue =
        VT == MVT::f32 ? UINT64_C(0x00400000)
                       : UINT64_C(0x0008000000000000);
    SDValue MagnitudeMask = DAG.getConstant(MagnitudeMaskValue, DL, BitsVT);
    SDValue ExponentMask = DAG.getConstant(ExponentMaskValue, DL, BitsVT);
    SDValue FractionMask = DAG.getConstant(FractionMaskValue, DL, BitsVT);
    SDValue QuietMask = DAG.getConstant(QuietMaskValue, DL, BitsVT);
    auto IntTest = [&](SDValue Left, SDValue Right, ISD::CondCode CC) {
      return DAG.getSetCC(DL, MVT::i64, Left, Right, CC);
    };
    auto SelectBits = [&](SDValue Condition, SDValue True, SDValue False) {
      return DAG.getNode(ISD::SELECT, DL, BitsVT, Condition, True, False);
    };
    auto IsNaN = [&](SDValue Bits) {
      SDValue HasMaxExponent = IntTest(
          DAG.getNode(ISD::AND, DL, BitsVT, Bits, ExponentMask), ExponentMask,
          ISD::SETEQ);
      SDValue HasFraction = IntTest(
          DAG.getNode(ISD::AND, DL, BitsVT, Bits, FractionMask), Zero,
          ISD::SETNE);
      return DAG.getNode(ISD::AND, DL, MVT::i64, HasMaxExponent, HasFraction);
    };

    SDValue LHSNaN = IsNaN(LHSBits);
    SDValue RHSNaN = IsNaN(RHSBits);
    SDValue LHSQuiet = IntTest(
        DAG.getNode(ISD::AND, DL, BitsVT, LHSBits, QuietMask), Zero,
        ISD::SETNE);
    SDValue RHSQuiet = IntTest(
        DAG.getNode(ISD::AND, DL, BitsVT, RHSBits, QuietMask), Zero,
        ISD::SETNE);
    SDValue LHSSignaling = DAG.getNode(
        ISD::AND, DL, MVT::i64, LHSNaN,
        DAG.getNode(ISD::XOR, DL, MVT::i64, LHSQuiet, One));
    SDValue RHSSignaling = DAG.getNode(
        ISD::AND, DL, MVT::i64, RHSNaN,
        DAG.getNode(ISD::XOR, DL, MVT::i64, RHSQuiet, One));
    SDValue QuietLHS =
        DAG.getNode(ISD::OR, DL, BitsVT, LHSBits, QuietMask);
    SDValue QuietRHS =
        DAG.getNode(ISD::OR, DL, BitsVT, RHSBits, QuietMask);
    SDValue QuietNaN = SelectBits(LHSSignaling, QuietLHS, QuietRHS);

    SDValue Ordered = SelectBits(
        DAG.getSetCC(DL, MVT::i64, LHS, RHS,
                     IsMax ? ISD::SETOGT : ISD::SETOLT),
        LHSBits, RHSBits);
    SDValue BothZero = DAG.getNode(
        ISD::AND, DL, MVT::i64,
        IntTest(DAG.getNode(ISD::AND, DL, BitsVT, LHSBits, MagnitudeMask), Zero,
                ISD::SETEQ),
        IntTest(DAG.getNode(ISD::AND, DL, BitsVT, RHSBits, MagnitudeMask), Zero,
                ISD::SETEQ));
    SDValue ZeroBits = DAG.getNode(IsMax ? ISD::AND : ISD::OR, DL, BitsVT,
                                   LHSBits, RHSBits);
    SDValue Numeric = SelectBits(BothZero, ZeroBits, Ordered);

    SDValue WithQuietNaNs = SelectBits(LHSNaN, RHSBits, Numeric);
    WithQuietNaNs = SelectBits(RHSNaN, LHSBits, WithQuietNaNs);
    SDValue AnySignaling = DAG.getNode(ISD::OR, DL, MVT::i64, LHSSignaling,
                                       RHSSignaling);
    SDValue ResultBits = SelectBits(AnySignaling, QuietNaN, WithQuietNaNs);
    return VT == MVT::f32
               ? DAG.getNode(MMIXISD::BITS_TO_F32, DL, MVT::f32, ResultBits)
               : DAG.getNode(ISD::BITCAST, DL, MVT::f64, ResultBits);
  }

  if (Op.getOpcode() == ISD::SETCC) {
    SDValue LHS = Op.getOperand(0);
    SDValue RHS = Op.getOperand(1);
    ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(2))->get();
    EVT ResultVT = Op.getValueType();
    SDValue Zero = DAG.getConstant(0, DL, ResultVT);
    SDValue One = DAG.getConstant(1, DL, ResultVT);
    auto FPCompare = [&](unsigned Opcode) {
      return DAG.getNode(Opcode, DL, ResultVT, LHS, RHS);
    };
    auto CompareZero = [&](SDValue Value, ISD::CondCode IntCC) {
      return DAG.getSetCC(DL, ResultVT, Value, Zero, IntCC);
    };

    // FEQL and FUN are quiet even for signaling NaNs. FCMP provides ordering,
    // but raises invalid for NaN operands; constrained comparisons therefore
    // remain unsupported instead of silently changing exception behavior.
    switch (CC) {
    case ISD::SETFALSE:
    case ISD::SETFALSE2:
      return Zero;
    case ISD::SETOEQ:
    case ISD::SETEQ:
      return FPCompare(MMIXISD::FEQL);
    case ISD::SETOGT:
    case ISD::SETGT:
      return CompareZero(FPCompare(MMIXISD::FCMP), ISD::SETGT);
    case ISD::SETOGE: {
      SDValue Ordered = CompareZero(FPCompare(MMIXISD::FUN), ISD::SETEQ);
      SDValue GE = CompareZero(FPCompare(MMIXISD::FCMP), ISD::SETGE);
      return DAG.getNode(ISD::AND, DL, ResultVT, Ordered, GE);
    }
    case ISD::SETGE:
      return CompareZero(FPCompare(MMIXISD::FCMP), ISD::SETGE);
    case ISD::SETOLT:
    case ISD::SETLT:
      return CompareZero(FPCompare(MMIXISD::FCMP), ISD::SETLT);
    case ISD::SETOLE: {
      SDValue Ordered = CompareZero(FPCompare(MMIXISD::FUN), ISD::SETEQ);
      SDValue LE = CompareZero(FPCompare(MMIXISD::FCMP), ISD::SETLE);
      return DAG.getNode(ISD::AND, DL, ResultVT, Ordered, LE);
    }
    case ISD::SETLE:
      return CompareZero(FPCompare(MMIXISD::FCMP), ISD::SETLE);
    case ISD::SETONE:
    case ISD::SETNE:
      return CompareZero(FPCompare(MMIXISD::FCMP), ISD::SETNE);
    case ISD::SETO:
      return CompareZero(FPCompare(MMIXISD::FUN), ISD::SETEQ);
    case ISD::SETUO:
      return FPCompare(MMIXISD::FUN);
    case ISD::SETUEQ:
      return DAG.getNode(ISD::OR, DL, ResultVT, FPCompare(MMIXISD::FEQL),
                         FPCompare(MMIXISD::FUN));
    case ISD::SETUGT: {
      SDValue GT = CompareZero(FPCompare(MMIXISD::FCMP), ISD::SETGT);
      return DAG.getNode(ISD::OR, DL, ResultVT, GT,
                         FPCompare(MMIXISD::FUN));
    }
    case ISD::SETUGE:
      return CompareZero(FPCompare(MMIXISD::FCMP), ISD::SETGE);
    case ISD::SETULT: {
      SDValue LT = CompareZero(FPCompare(MMIXISD::FCMP), ISD::SETLT);
      return DAG.getNode(ISD::OR, DL, ResultVT, LT,
                         FPCompare(MMIXISD::FUN));
    }
    case ISD::SETULE:
      return CompareZero(FPCompare(MMIXISD::FCMP), ISD::SETLE);
    case ISD::SETUNE:
      return CompareZero(FPCompare(MMIXISD::FEQL), ISD::SETEQ);
    case ISD::SETTRUE:
    case ISD::SETTRUE2:
      return One;
    default:
      llvm_unreachable("unexpected floating-point condition code");
    }
  }

  if (Op.getOpcode() == ISD::GlobalAddress ||
      Op.getOpcode() == ISD::ExternalSymbol ||
      Op.getOpcode() == ISD::BlockAddress ||
      Op.getOpcode() == ISD::ConstantPool || Op.getOpcode() == ISD::JumpTable) {
    if (getTargetMachine().getRelocationModel() != Reloc::Static)
      report_fatal_error("MMIX supports only the static relocation model");

    SDValue Target;
    if (auto *GA = dyn_cast<GlobalAddressSDNode>(Op))
      Target = DAG.getTargetGlobalAddress(GA->getGlobal(), DL, MVT::i64,
                                          GA->getOffset());
    else if (auto *ES = dyn_cast<ExternalSymbolSDNode>(Op))
      Target = DAG.getTargetExternalSymbol(ES->getSymbol(), MVT::i64);
    else if (auto *BA = dyn_cast<BlockAddressSDNode>(Op))
      Target = DAG.getTargetBlockAddress(BA->getBlockAddress(), MVT::i64,
                                         BA->getOffset());
    else if (auto *CP = dyn_cast<ConstantPoolSDNode>(Op)) {
      if (CP->isMachineConstantPoolEntry())
        Target = DAG.getTargetConstantPool(CP->getMachineCPVal(), MVT::i64,
                                           CP->getAlign(), CP->getOffset());
      else
        Target = DAG.getTargetConstantPool(CP->getConstVal(), MVT::i64,
                                           CP->getAlign(), CP->getOffset());
    } else {
      auto *JT = cast<JumpTableSDNode>(Op);
      Target = DAG.getTargetJumpTable(JT->getIndex(), MVT::i64);
    }
    return DAG.getNode(MMIXISD::LOAD_ADDR, DL, MVT::i64, Target);
  }

  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDVTList PairVTs = DAG.getVTList(MVT::i64, MVT::i64);

  switch (Op.getOpcode()) {
  case ISD::UMUL_LOHI:
    return DAG.getNode(MMIXISD::UMUL_LOHI, DL, PairVTs, LHS, RHS);
  case ISD::SMUL_LOHI: {
    SDValue Product = DAG.getNode(MMIXISD::UMUL_LOHI, DL, PairVTs, LHS, RHS);
    SDValue Shift = DAG.getConstant(63, DL, MVT::i64);
    SDValue LHSMask = DAG.getNode(ISD::SRA, DL, MVT::i64, LHS, Shift);
    SDValue RHSMask = DAG.getNode(ISD::SRA, DL, MVT::i64, RHS, Shift);
    SDValue LHSCorrection = DAG.getNode(ISD::AND, DL, MVT::i64, LHSMask, RHS);
    SDValue RHSCorrection = DAG.getNode(ISD::AND, DL, MVT::i64, RHSMask, LHS);
    SDValue High =
        DAG.getNode(ISD::SUB, DL, MVT::i64, Product.getValue(1), LHSCorrection);
    High = DAG.getNode(ISD::SUB, DL, MVT::i64, High, RHSCorrection);
    return DAG.getMergeValues({Product, High}, DL);
  }
  case ISD::UDIVREM:
    return DAG.getNode(MMIXISD::UDIVREM, DL, PairVTs, LHS, RHS);
  case ISD::SDIVREM: {
    SDValue Floor = DAG.getNode(MMIXISD::SDIVREM, DL, PairVTs, LHS, RHS);
    SDValue Zero = DAG.getConstant(0, DL, MVT::i64);
    SDValue Shift = DAG.getConstant(63, DL, MVT::i64);
    SDValue SignDifference = DAG.getNode(ISD::XOR, DL, MVT::i64, LHS, RHS);
    SignDifference = DAG.getNode(ISD::SRA, DL, MVT::i64, SignDifference, Shift);
    SDValue NegativeRemainder =
        DAG.getNode(ISD::SUB, DL, MVT::i64, Zero, Floor.getValue(1));
    SDValue NonZeroRemainder = DAG.getNode(
        ISD::OR, DL, MVT::i64, Floor.getValue(1), NegativeRemainder);
    NonZeroRemainder =
        DAG.getNode(ISD::SRA, DL, MVT::i64, NonZeroRemainder, Shift);
    SDValue Correction =
        DAG.getNode(ISD::AND, DL, MVT::i64, SignDifference, NonZeroRemainder);
    SDValue Quotient = DAG.getNode(ISD::SUB, DL, MVT::i64, Floor, Correction);
    SDValue RemainderCorrection =
        DAG.getNode(ISD::AND, DL, MVT::i64, RHS, Correction);
    SDValue Remainder = DAG.getNode(ISD::SUB, DL, MVT::i64, Floor.getValue(1),
                                    RemainderCorrection);
    return DAG.getMergeValues({Quotient, Remainder}, DL);
  }
  default:
    llvm_unreachable("unexpected custom MMIX operation");
  }
}

static bool hasUnsupportedABIFlags(const ISD::ArgFlagsTy &Flags) {
  return Flags.isInReg() || Flags.isByRef() || Flags.isNest() ||
         Flags.isInAlloca() || Flags.isPreallocated() || Flags.isSwiftSelf() ||
         Flags.isSwiftAsync() || Flags.isSwiftError() ||
         Flags.isCFGuardTarget() || Flags.isHva() || Flags.isHvaStart() ||
         Flags.isSecArgPass();
}

static bool isMMIXWideComplexResultType(Type *Ty) {
  auto *StructTy = dyn_cast_or_null<StructType>(Ty);
  return StructTy && !StructTy->isPacked() && StructTy->getNumElements() == 2 &&
         StructTy->getElementType(0)->isDoubleTy() &&
         StructTy->getElementType(1)->isDoubleTy();
}

static MMIXAggregateABIClassification classifyMMIXABIValue(
    MMIXAggregateABIRole Role, const ISD::ArgFlagsTy &Flags,
    const DataLayout &DL, Type *AggregateTy = nullptr,
    unsigned NumParts = 1) {
  bool IsDirectAggregate = AggregateTy && AggregateTy->isAggregateType() &&
                           !Flags.isByVal() && !Flags.isSRet();
  // A scalar i128 is legalized into two ordinary big-endian octa slots.
  // This does not change direct aggregate or unsupported-width classification.
  bool IsWideInteger = AggregateTy && AggregateTy->isIntegerTy(128) &&
                       !Flags.isByVal() && !Flags.isSRet();
  MMIXAggregateABIValue Value;
  Value.Role = Flags.isSRet() ? MMIXAggregateABIRole::Result : Role;
  Value.IsAggregate = AggregateTy && AggregateTy->isAggregateType();
  Value.IsByVal = Flags.isByVal();
  Value.IsSRet = Flags.isSRet();
  Value.IsSplit = !IsDirectAggregate && !IsWideInteger &&
                  (Flags.isSplit() || Flags.isSplitEnd());
  Value.IsInConsecutiveRegs = !IsDirectAggregate && !IsWideInteger &&
                             (Flags.isInConsecutiveRegs() ||
                              Flags.isInConsecutiveRegsLast());
  Value.HasUnsupportedFlags = hasUnsupportedABIFlags(Flags);
  Value.AddressSpace =
      Flags.isPointer() ? Flags.getPointerAddrSpace() : 0;
  Value.NumParts =
      IsDirectAggregate || (IsWideInteger && NumParts <= 2) ? 1 : NumParts;

  if (Flags.isByVal()) {
    Value.Size = Flags.getByValSize();
    Value.Alignment = Flags.getNonZeroByValAlign();
  } else if (AggregateTy && AggregateTy->isAggregateType()) {
    Align FlagAlignment = Flags.getNonZeroMemAlign();
    Value.Alignment = FlagAlignment;
    if (AggregateTy->isSized()) {
      TypeSize Size = DL.getTypeAllocSize(AggregateTy);
      if (!Size.isScalable()) {
        Value.Size = Size.getFixedValue();
        Value.Alignment =
            std::max(FlagAlignment, DL.getABITypeAlign(AggregateTy));
      }
    }
  }

  MMIXAggregateABIClassification Classification =
      classifyMMIXAggregateABI(Value);
  // Clang selects { double, double } as the direct IR result for the standard
  // binary64 complex types. Ordinary two-double C records use sret and never
  // reach this direct-result exception.
  if (Role == MMIXAggregateABIRole::Result &&
      isMMIXWideComplexResultType(AggregateTy) && Value.Size == 16 &&
      Value.Alignment <= Align(8) && Value.AddressSpace == 0 &&
      !Value.IsByVal && !Value.IsSRet && !Value.IsSplit &&
      !Value.IsInConsecutiveRegs && !Value.HasUnsupportedFlags)
    return {MMIXAggregateABIKind::DirectResult,
            MMIXAggregateABIError::None};
  return Classification;
}

static MMIXTailCallResultShape
classifyMMIXCallerTailResult(const TargetLowering &TLI, const Function &F,
                             const DataLayout &DL) {
  if (F.hasStructRetAttr())
    return MMIXTailCallResultShape::Indirect;

  SmallVector<EVT, 2> ValueVTs;
  ComputeValueVTs(TLI, DL, F.getReturnType(), ValueVTs);
  ISD::ArgFlagsTy Flags;
  MMIXAggregateABIClassification Classification =
      classifyMMIXABIValue(MMIXAggregateABIRole::Result, Flags, DL,
                           F.getReturnType(), ValueVTs.size());
  if (!Classification.isValid())
    return MMIXTailCallResultShape::Unsupported;

  if (F.getReturnType()->isIntegerTy(128))
    return MMIXTailCallResultShape::TwoRegisters;

  unsigned NumResultRegisters = ValueVTs.size();
  if (Classification.Kind == MMIXAggregateABIKind::DirectResult)
    NumResultRegisters = isMMIXWideComplexResultType(F.getReturnType()) ? 2 : 1;
  return classifyMMIXTailCallResultShape(Classification.Kind,
                                         NumResultRegisters);
}

static bool hasCompatibleMMIXTailResultAttributes(const Function &Caller,
                                                  const CallBase *Call) {
  if (!Call || Caller.getReturnType() != Call->getType())
    return false;
  for (Attribute::AttrKind Kind : {Attribute::SExt, Attribute::ZExt})
    if (Caller.hasRetAttribute(Kind) != Call->hasRetAttr(Kind))
      return false;
  return true;
}

static std::optional<unsigned>
getMMIXFixedVectorABIWidth(Type *Ty, const DataLayout &DL) {
  const auto *VectorTy = dyn_cast<FixedVectorType>(Ty);
  if (!VectorTy)
    return std::nullopt;

  unsigned Lanes = VectorTy->getNumElements();
  if (!isPowerOf2_32(Lanes))
    return std::nullopt;

  Type *ElementTy = VectorTy->getElementType();
  unsigned ElementWidth;
  if (ElementTy->isIntegerTy(1)) {
    if (Lanes < 8)
      return std::nullopt;
    ElementWidth = 1;
  } else if (ElementTy->isIntegerTy()) {
    ElementWidth = ElementTy->getIntegerBitWidth();
    if (ElementWidth != 8 && ElementWidth != 16 && ElementWidth != 32 &&
        ElementWidth != 64)
      return std::nullopt;
  } else if (ElementTy->isFloatTy()) {
    ElementWidth = 32;
  } else if (ElementTy->isDoubleTy()) {
    ElementWidth = 64;
  } else {
    return std::nullopt;
  }

  if (Lanes > 64 / ElementWidth)
    return std::nullopt;
  unsigned Width = ElementWidth * Lanes;
  if (Width != 8 && Width != 16 && Width != 32 && Width != 64)
    return std::nullopt;

  TypeSize AllocSize = DL.getTypeAllocSize(Ty);
  if (AllocSize.isScalable() || AllocSize.getFixedValue() != Width / 8 ||
      DL.getABITypeAlign(Ty) != Align(Width / 8))
    return std::nullopt;
  return Width;
}

static bool isSupportedMMIXABIType(Type *Ty, const DataLayout &DL) {
  if (Ty->isVectorTy())
    return getMMIXFixedVectorABIWidth(Ty, DL).has_value();
  if (Ty->isVoidTy() || Ty->isAggregateType() || Ty->isPointerTy() ||
      Ty->isFloatTy() || Ty->isDoubleTy())
    return true;
  return Ty->isIntegerTy() &&
         (Ty->getIntegerBitWidth() <= 64 || Ty->isIntegerTy(128));
}

[[noreturn]] static void reportUnsupportedMMIXABIType(
    Type *Ty, StringRef ValueKind, StringRef FunctionName) {
  std::string TypeName;
  raw_string_ostream OS(TypeName);
  Ty->print(OS);
  reportFatalUsageError(Twine("MMIX does not support ABI type '") + OS.str() +
                        "' for " + ValueKind + " in function '" +
                        FunctionName + "'");
}

[[noreturn]] static void reportMMIXABIClassificationError(
    const MMIXAggregateABIClassification &Classification, StringRef ValueKind,
    StringRef FunctionName) {
  StringRef Reason;
  switch (Classification.Error) {
  case MMIXAggregateABIError::VariableSize:
    Reason = "variable-size";
    break;
  case MMIXAggregateABIError::OverAligned:
    Reason = "over-aligned";
    break;
  case MMIXAggregateABIError::NonZeroAddressSpace:
    Reason = "nonzero-address-space";
    break;
  case MMIXAggregateABIError::Split:
    Reason = "split";
    break;
  case MMIXAggregateABIError::MultiRegister:
    Reason = "multi-register";
    break;
  case MMIXAggregateABIError::UnsupportedFlags:
    reportFatalUsageError(Twine("MMIX does not support ") + ValueKind +
                          " with unsupported ABI flags in function '" +
                          FunctionName + "'");
  case MMIXAggregateABIError::None:
    llvm_unreachable("valid MMIX ABI classification has no diagnostic");
  }
  reportFatalUsageError(Twine("MMIX does not support ") + Reason + " " +
                        ValueKind + " in function '" + FunctionName + "'");
}

static SmallVector<uint64_t, 4>
getMMIXAggregatePartOffsets(Type *AggregateTy, const DataLayout &DL) {
  SmallVector<Type *, 4> PartTypes;
  SmallVector<TypeSize, 4> PartOffsets;
  ComputeValueTypes(DL, AggregateTy, PartTypes, &PartOffsets);

  SmallVector<uint64_t, 4> FixedOffsets;
  FixedOffsets.reserve(PartOffsets.size());
  for (TypeSize Offset : PartOffsets) {
    if (Offset.isScalable())
      report_fatal_error("MMIX does not support scalable aggregate offsets");
    FixedOffsets.push_back(Offset.getFixedValue());
  }
  return FixedOffsets;
}

static uint64_t getMMIXAggregateSize(Type *AggregateTy,
                                     const DataLayout &DL) {
  TypeSize Size = DL.getTypeAllocSize(AggregateTy);
  if (Size.isScalable())
    report_fatal_error("MMIX does not support scalable aggregate sizes");
  return Size.getFixedValue();
}

static SDValue getMMIXAggregatePartBits(SDValue Part, EVT PartVT,
                                        const SDLoc &DL,
                                        SelectionDAG &DAG) {
  SDValue Bits;
  if (PartVT == MVT::f32)
    Bits = DAG.getNode(MMIXISD::F32_TO_BITS, DL, MVT::i64, Part);
  else if (PartVT == MVT::f64)
    Bits = DAG.getNode(ISD::BITCAST, DL, MVT::i64, Part);
  else if (Part.getValueType() == MVT::i64)
    Bits = Part;
  else if (Part.getValueType().isInteger())
    Bits = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, Part);
  else
    report_fatal_error("MMIX cannot pack this direct aggregate field type");

  if (PartVT.isInteger() && PartVT.getSizeInBits() < 64) {
    uint64_t Mask = maskTrailingOnes<uint64_t>(PartVT.getSizeInBits());
    Bits = DAG.getNode(ISD::AND, DL, MVT::i64, Bits,
                       DAG.getConstant(Mask, DL, MVT::i64));
  }
  return Bits;
}

static SDValue packMMIXDirectAggregate(
    ArrayRef<ISD::OutputArg> Parts, ArrayRef<SDValue> PartValues,
    ArrayRef<uint64_t> PartOffsets, uint64_t AggregateSize, const SDLoc &DL,
    SelectionDAG &DAG) {
  if (Parts.size() != PartValues.size() || Parts.size() != PartOffsets.size())
    report_fatal_error("MMIX direct aggregate lowering received mismatched "
                       "field metadata");

  SDValue Packed = DAG.getConstant(0, DL, MVT::i64);
  for (unsigned I = 0; I != Parts.size(); ++I) {
    TypeSize PartSize = Parts[I].ArgVT.getStoreSize();
    if (PartSize.isScalable())
      report_fatal_error("MMIX does not support scalable aggregate fields");
    uint64_t FixedPartSize = PartSize.getFixedValue();
    if (PartOffsets[I] + FixedPartSize > AggregateSize)
      report_fatal_error("MMIX direct aggregate field exceeds its object");

    SDValue Bits =
        getMMIXAggregatePartBits(PartValues[I], Parts[I].ArgVT, DL, DAG);
    // MMIX places object byte zero at the most significant occupied byte and
    // right-justifies the complete object in its octa-sized ABI slot.
    unsigned Shift = 8 * (AggregateSize - PartOffsets[I] - FixedPartSize);
    if (Shift)
      Bits = DAG.getNode(ISD::SHL, DL, MVT::i64, Bits,
                         DAG.getConstant(Shift, DL, MVT::i64));
    Packed = DAG.getNode(ISD::OR, DL, MVT::i64, Packed, Bits);
  }
  return Packed;
}

static SDValue unpackMMIXDirectAggregatePart(
    SDValue Packed, const ISD::InputArg &Part, uint64_t PartOffset,
    uint64_t AggregateSize, const SDLoc &DL, SelectionDAG &DAG) {
  TypeSize PartSize = Part.ArgVT.getStoreSize();
  if (PartSize.isScalable())
    report_fatal_error("MMIX does not support scalable aggregate fields");
  uint64_t FixedPartSize = PartSize.getFixedValue();
  if (PartOffset + FixedPartSize > AggregateSize)
    report_fatal_error("MMIX direct aggregate field exceeds its object");

  unsigned Shift = 8 * (AggregateSize - PartOffset - FixedPartSize);
  SDValue Bits = Packed;
  if (Shift)
    Bits = DAG.getNode(ISD::SRL, DL, MVT::i64, Bits,
                       DAG.getConstant(Shift, DL, MVT::i64));

  if (Part.ArgVT == MVT::f32)
    return DAG.getNode(MMIXISD::BITS_TO_F32, DL, MVT::f32, Bits);
  if (Part.ArgVT == MVT::f64)
    return DAG.getNode(ISD::BITCAST, DL, MVT::f64, Bits);
  if (!Part.ArgVT.isInteger() && !Part.Flags.isPointer())
    report_fatal_error("MMIX cannot unpack this direct aggregate field type");
  if (Part.VT == MVT::i64)
    return Bits;
  return DAG.getNode(ISD::TRUNCATE, DL, Part.VT, Bits);
}

static Type *getMMIXFixedVectorFormalArgumentType(const TargetLowering &TLI,
                                                  const Function &F,
                                                  const DataLayout &DL,
                                                  unsigned PartIndex) {
  unsigned FirstPart = 0;
  for (const Argument &Arg : F.args()) {
    SmallVector<EVT, 4> ValueVTs;
    ComputeValueVTs(TLI, DL, Arg.getType(), ValueVTs);
    if (PartIndex < FirstPart + ValueVTs.size())
      return Arg.getType()->isVectorTy() ? Arg.getType() : nullptr;
    FirstPart += ValueVTs.size();
  }
  return nullptr;
}

struct MMIXFormalArgMapping {
  SmallVector<unsigned, 4> OriginalParts;
  SmallVector<uint64_t, 4> PartOffsets;
  uint64_t AggregateSize = 0;
  uint64_t LocalCopySize = 0;
  Align LocalCopyAlignment = Align(1);
  bool IsFixedVector = false;

  bool isDirectAggregate() const { return !PartOffsets.empty(); }
  bool isFixedVector() const { return IsFixedVector; }
  bool needsLocalCopy() const { return LocalCopySize != 0; }
};

static bool isSupportedCallValueType(EVT VT) {
  return VT == MVT::i1 || VT == MVT::i8 || VT == MVT::i16 || VT == MVT::i32 ||
         VT == MVT::i64 || VT == MVT::f32 || VT == MVT::f64;
}

static ISD::ArgFlagsTy getMMIXOrdinaryPointerArgFlags() {
  ISD::ArgFlagsTy Flags;
  Flags.setPointer();
  Flags.setPointerAddrSpace(0);
  Flags.setOrigAlign(Align(8));
  return Flags;
}

static SDValue convertMMIXCallBits(SDValue Value, EVT ResultVT, const SDLoc &DL,
                                   SelectionDAG &DAG) {
  if (Value.getValueType() == MVT::f32 && ResultVT == MVT::i64)
    return DAG.getNode(MMIXISD::F32_TO_BITS, DL, MVT::i64, Value);
  if (Value.getValueType() == MVT::i64 && ResultVT == MVT::f32)
    return DAG.getNode(MMIXISD::BITS_TO_F32, DL, MVT::f32, Value);
  return DAG.getNode(ISD::BITCAST, DL, ResultVT, Value);
}

static SDValue convertOutgoingValue(SDValue Value, const CCValAssign &VA,
                                    const SDLoc &DL, SelectionDAG &DAG) {
  switch (VA.getLocInfo()) {
  case CCValAssign::Full:
    return Value;
  case CCValAssign::SExt:
    return DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), Value);
  case CCValAssign::ZExt:
    return DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), Value);
  case CCValAssign::AExt:
    return DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), Value);
  case CCValAssign::BCvt:
    return convertMMIXCallBits(Value, VA.getLocVT(), DL, DAG);
  default:
    report_fatal_error("MMIX does not support this call operand conversion");
  }
}

static void copyMMIXArgumentsToRegisters(
    SDValue &Chain, SDValue &Glue,
    ArrayRef<std::pair<MCRegister, SDValue>> RegsToPass, const SDLoc &DL,
    SelectionDAG &DAG) {
  // Keeping every source as an SDValue until all assignments are known gives
  // the scheduler and register allocator parallel-copy semantics. In
  // particular, no early physical-register write can destroy a later source
  // or an overlapping indirect callee.
  for (const auto &[Reg, Value] : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, DL, Reg, Value, Glue);
    Glue = Chain.getValue(1);
  }
}

static SDValue addTokenForMMIXTailCallArgument(
    SDValue Chain, SelectionDAG &DAG, MachineFrameInfo &MFI,
    int ClobberedFrameIndex) {
  SmallVector<SDValue, 8> ArgChains = {Chain};
  int64_t FirstByte = MFI.getObjectOffset(ClobberedFrameIndex);
  int64_t LastByte =
      FirstByte + int64_t(MFI.getObjectSize(ClobberedFrameIndex)) - 1;

  // Order every overlapping incoming fixed-stack load before the outgoing tail
  // store so arbitrary stack-argument permutations have memmove-equivalent
  // behavior.
  for (SDNode *User : DAG.getEntryNode()->users()) {
    auto *Load = dyn_cast<LoadSDNode>(User);
    if (!Load)
      continue;
    const auto *FI = dyn_cast<FrameIndexSDNode>(Load->getBasePtr());
    if (!FI || FI->getIndex() >= 0)
      continue;
    int64_t InFirstByte = MFI.getObjectOffset(FI->getIndex());
    int64_t InLastByte =
        InFirstByte + int64_t(MFI.getObjectSize(FI->getIndex())) - 1;
    if (InFirstByte <= LastByte && FirstByte <= InLastByte)
      ArgChains.push_back(SDValue(Load, 1));
  }

  return DAG.getNode(ISD::TokenFactor, SDLoc(Chain), MVT::Other, ArgChains);
}

static void
validateMMIXVariadicCallOperands(const TargetLowering::CallLoweringInfo &CLI,
                                 StringRef FunctionName) {
  if (!CLI.IsVarArg)
    return;
  if (CLI.NumFixedArgs > CLI.Args.size())
    report_fatal_error("MMIX variadic call has invalid fixed-argument state");

  for (unsigned I = CLI.NumFixedArgs; I != CLI.Args.size(); ++I) {
    const TargetLowering::ArgListEntry &Arg = CLI.Args[I];
    Type *Ty = Arg.OrigTy;
    if (Ty->isIntegerTy() && Ty->getIntegerBitWidth() < 32 && !Arg.IsNoExt)
      reportFatalUsageError(
          Twine(
              "MMIX requires variadic integer call arguments narrower than ") +
          "i32 to be promoted in function '" + FunctionName + "'");
    if (Ty->isFloatTy())
      reportFatalUsageError(
          Twine("MMIX requires variadic float call arguments to be promoted ") +
          "to double in function '" + FunctionName + "'");
    if (Ty->isIntegerTy(32) && !Arg.IsNoExt && Arg.IsSExt == Arg.IsZExt)
      reportFatalUsageError(
          Twine("MMIX requires variadic i32 call arguments to carry exactly ") +
          "one of signext or zeroext in function '" + FunctionName + "'");
  }
}

static bool isSingleMMIXVectorABISlot(EVT VT) {
  return VT.isVector() && !VT.isScalableVector() &&
         VT.getSizeInBits().getFixedValue() <= 64;
}

MVT MMIXTargetLowering::getRegisterTypeForCallingConv(
    LLVMContext &Context, CallingConv::ID CC, EVT VT) const {
  if (isSingleMMIXVectorABISlot(VT))
    return MVT::i64;
  return TargetLowering::getRegisterTypeForCallingConv(Context, CC, VT);
}

unsigned MMIXTargetLowering::getNumRegistersForCallingConv(
    LLVMContext &Context, CallingConv::ID CC, EVT VT) const {
  if (isSingleMMIXVectorABISlot(VT))
    return 1;
  return TargetLowering::getNumRegistersForCallingConv(Context, CC, VT);
}

unsigned MMIXTargetLowering::getVectorTypeBreakdownForCallingConv(
    LLVMContext &Context, CallingConv::ID CC, EVT VT, EVT &IntermediateVT,
    unsigned &NumIntermediates, MVT &RegisterVT) const {
  if (isSingleMMIXVectorABISlot(VT)) {
    IntermediateVT = MVT::i64;
    NumIntermediates = 1;
    RegisterVT = MVT::i64;
    return 1;
  }
  return TargetLowering::getVectorTypeBreakdownForCallingConv(
      Context, CC, VT, IntermediateVT, NumIntermediates, RegisterVT);
}

SDValue MMIXTargetLowering::LowerCall(CallLoweringInfo &CLI,
                                      SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = CLI.DAG.getMachineFunction();
  SelectionDAG &DAG = CLI.DAG;
  SDValue Chain = CLI.Chain;
  bool IsRequiredTailCall = CLI.CB && CLI.CB->isMustTailCall();
  bool TailCallRequested = CLI.IsTailCall || IsRequiredTailCall;
  if (!isSupportedMMIXCallingConv(CLI.CallConv))
    reportFatalUsageError(
        Twine("MMIX supports only C and Fast calling conventions in ") +
        "function '" + MF.getName() + "'");
  validateMMIXVariadicCallOperands(CLI, MF.getName());
  const DataLayout &DataLayout = DAG.getDataLayout();
  std::optional<unsigned> VectorResultWidth =
      getMMIXFixedVectorABIWidth(CLI.OrigRetTy, DataLayout);
  if (!isSupportedMMIXABIType(CLI.OrigRetTy, DataLayout))
    reportUnsupportedMMIXABIType(CLI.OrigRetTy, "call results", MF.getName());
  ISD::ArgFlagsTy ResultFlags;
  if (!CLI.Ins.empty())
    ResultFlags = CLI.Ins.front().Flags;
  MMIXAggregateABIClassification ResultClassification = classifyMMIXABIValue(
      MMIXAggregateABIRole::Result, ResultFlags, CLI.DAG.getDataLayout(),
      CLI.OrigRetTy, CLI.Ins.size());
  bool IsWideComplexResult = isMMIXWideComplexResultType(CLI.OrigRetTy);
  if (!ResultClassification.isValid())
    reportMMIXABIClassificationError(ResultClassification, "call results",
                                     MF.getName());
  if (ResultClassification.isAggregate() &&
      ResultClassification.Kind != MMIXAggregateABIKind::Empty &&
      ResultClassification.Kind != MMIXAggregateABIKind::DirectResult)
    reportFatalUsageError(
        Twine("MMIX does not support this aggregate call result form in ") +
        "function '" + MF.getName() + "'");

  SmallVector<ISD::InputArg, 4> ABIIns;
  SmallVector<uint64_t, 4> ResultPartOffsets;
  uint64_t ResultAggregateSize = 0;
  if (VectorResultWidth) {
    if (CLI.Ins.size() != 1 || CLI.Ins.front().VT != MVT::i64)
      reportFatalUsageError(
          Twine("MMIX cannot lower this fixed-vector call result in function '") +
          MF.getName() + "'");
    ISD::ArgFlagsTy PackedFlags;
    PackedFlags.setOrigAlign(DataLayout.getABITypeAlign(CLI.OrigRetTy));
    Type *I64Ty = Type::getInt64Ty(*DAG.getContext());
    ABIIns.emplace_back(PackedFlags, MVT::i64, MVT::i64, I64Ty,
                        CLI.Ins.front().Used, ISD::InputArg::NoArgIndex, 0);
  } else if (ResultClassification.Kind == MMIXAggregateABIKind::DirectResult) {
    for (const ISD::InputArg &Result : CLI.Ins)
      if (!isSupportedCallValueType(Result.VT))
        reportFatalUsageError(
            Twine("MMIX does not support aggregate call results in ") +
            "function '" + MF.getName() + "'");

    ResultPartOffsets =
        getMMIXAggregatePartOffsets(CLI.OrigRetTy, DAG.getDataLayout());
    if (ResultPartOffsets.size() != CLI.Ins.size())
      reportFatalUsageError(
          Twine("MMIX cannot map direct aggregate call result fields in ") +
          "function '" + MF.getName() + "'");
    ResultAggregateSize =
        getMMIXAggregateSize(CLI.OrigRetTy, DAG.getDataLayout());

    if (IsWideComplexResult) {
      if (CLI.Ins.size() != 2 ||
          llvm::any_of(CLI.Ins, [](const ISD::InputArg &Arg) {
            return Arg.VT != MVT::f64;
          }))
        reportFatalUsageError(
            Twine("MMIX cannot lower this wide complex call result in ") +
            "function '" + MF.getName() + "'");
      ABIIns.append(CLI.Ins.begin(), CLI.Ins.end());
    } else {
      bool Used = llvm::any_of(
          CLI.Ins, [](const ISD::InputArg &Arg) { return Arg.Used; });
      ISD::ArgFlagsTy PackedFlags;
      PackedFlags.setOrigAlign(
          DAG.getDataLayout().getABITypeAlign(CLI.OrigRetTy));
      Type *I64Ty = Type::getInt64Ty(*DAG.getContext());
      ABIIns.emplace_back(PackedFlags, MVT::i64, MVT::i64, I64Ty, Used,
                          ISD::InputArg::NoArgIndex, 0);
    }
  } else if (ResultClassification.Kind == MMIXAggregateABIKind::Empty) {
    if (!CLI.Ins.empty())
      report_fatal_error("MMIX empty aggregate call result has value parts");
  } else {
    if (CLI.Ins.size() > (CLI.OrigRetTy->isIntegerTy(128) ? 2u : 1u))
      reportFatalUsageError(
          Twine("MMIX cannot lower this scalar call result representation in ") +
          "function '" + MF.getName() + "'");
    for (const ISD::InputArg &Result : CLI.Ins) {
      MMIXAggregateABIClassification Classification = classifyMMIXABIValue(
          MMIXAggregateABIRole::Result, Result.Flags, DAG.getDataLayout(),
          CLI.OrigRetTy, CLI.Ins.size());
      if (!Classification.isValid())
        reportMMIXABIClassificationError(Classification, "call results",
                                         MF.getName());
      if (!isSupportedCallValueType(Result.VT) ||
          Classification.isAggregate())
        reportFatalUsageError(
            Twine("MMIX cannot lower this call result representation in ") +
            "function '" + MF.getName() + "'");
      ABIIns.push_back(Result);
    }
  }
  if (CLI.Outs.size() != CLI.OutVals.size())
    report_fatal_error("MMIX call operand lowering received mismatched values");

  SmallVector<ISD::OutputArg, 16> ABIOuts;
  SmallVector<SDValue, 16> ABIOutVals;
  bool HasCallerCopy = false;
  for (unsigned I = 0; I != CLI.Outs.size();) {
    const ISD::OutputArg &Arg = CLI.Outs[I];
    Type *AggregateTy = nullptr;
    if (Arg.OrigArgIndex < CLI.Args.size()) {
      const ArgListEntry &OriginalArg = CLI.Args[Arg.OrigArgIndex];
      AggregateTy = OriginalArg.IndirectType
                        ? OriginalArg.IndirectType
                        : OriginalArg.OrigTy;
    }
    if (AggregateTy && !isSupportedMMIXABIType(AggregateTy, DataLayout))
      reportUnsupportedMMIXABIType(AggregateTy, "call arguments",
                                   MF.getName());
    std::optional<unsigned> VectorWidth =
        AggregateTy ? getMMIXFixedVectorABIWidth(AggregateTy, DataLayout)
                    : std::nullopt;
    if (VectorWidth) {
      if (CLI.IsVarArg || Arg.VT != MVT::i64)
        reportFatalUsageError(
            Twine("MMIX does not support variadic or split fixed-vector call ") +
            "arguments in function '" + MF.getName() + "'");
      ISD::ArgFlagsTy PackedFlags;
      PackedFlags.setOrigAlign(DataLayout.getABITypeAlign(AggregateTy));
      Type *I64Ty = Type::getInt64Ty(*DAG.getContext());
      ABIOuts.emplace_back(PackedFlags, MVT::i64, MVT::i64, I64Ty,
                           Arg.OrigArgIndex, 0);
      ABIOutVals.push_back(CLI.OutVals[I]);
      ++I;
      continue;
    }
    MMIXAggregateABIClassification Classification = classifyMMIXABIValue(
        MMIXAggregateABIRole::Argument, Arg.Flags, CLI.DAG.getDataLayout(),
        AggregateTy);
    if (!Classification.isValid())
      reportMMIXABIClassificationError(Classification, "call arguments",
                                       MF.getName());
    if (!isSupportedCallValueType(Arg.VT) ||
        (Classification.isAggregate() &&
         Classification.Kind != MMIXAggregateABIKind::DirectArgument &&
         Classification.Kind != MMIXAggregateABIKind::CallerCopyArgument &&
         Classification.Kind != MMIXAggregateABIKind::IndirectResult))
      reportFatalUsageError(
          Twine("MMIX cannot lower this call argument representation ") +
          "in function '" + MF.getName() + "'");

    if (Classification.Kind == MMIXAggregateABIKind::CallerCopyArgument) {
      HasCallerCopy = true;
      uint64_t Size = Arg.Flags.getByValSize();
      Align Alignment = Arg.Flags.getNonZeroByValAlign();
      int FI = MF.getFrameInfo().CreateStackObject(Size, Alignment,
                                                   /*isSS=*/false);
      SDValue CopyAddress =
          DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
      SDValue SizeNode = DAG.getConstant(Size, CLI.DL, MVT::i64);
      Chain = DAG.getMemcpy(
          Chain, CLI.DL, CopyAddress, CLI.OutVals[I], SizeNode, Alignment,
          Alignment, /*isVol=*/false, /*AlwaysInline=*/false, /*CI=*/nullptr,
          std::nullopt, MachinePointerInfo(), MachinePointerInfo());

      ISD::ArgFlagsTy PointerFlags = getMMIXOrdinaryPointerArgFlags();
      Type *PointerTy = PointerType::getUnqual(*DAG.getContext());
      ABIOuts.emplace_back(PointerFlags, MVT::i64, MVT::i64, PointerTy,
                           Arg.OrigArgIndex, 0);
      ABIOutVals.push_back(CopyAddress);
      ++I;
      continue;
    }

    if (Classification.Kind != MMIXAggregateABIKind::DirectArgument) {
      ABIOuts.push_back(Arg);
      ABIOutVals.push_back(CLI.OutVals[I]);
      ++I;
      continue;
    }

    unsigned End = I + 1;
    while (End != CLI.Outs.size() &&
           CLI.Outs[End].OrigArgIndex == Arg.OrigArgIndex)
      ++End;
    for (unsigned Part = I; Part != End; ++Part) {
      if (!isSupportedCallValueType(CLI.Outs[Part].VT))
        reportFatalUsageError(
            Twine("MMIX does not support non-scalar direct aggregate fields ") +
            "in function '" + MF.getName() + "'");
    }

    SmallVector<uint64_t, 4> PartOffsets =
        getMMIXAggregatePartOffsets(AggregateTy, CLI.DAG.getDataLayout());
    uint64_t AggregateSize =
        getMMIXAggregateSize(AggregateTy, CLI.DAG.getDataLayout());
    SDValue Packed = packMMIXDirectAggregate(
        ArrayRef(CLI.Outs).slice(I, End - I),
        ArrayRef(CLI.OutVals).slice(I, End - I), PartOffsets, AggregateSize,
        CLI.DL, CLI.DAG);

    ISD::ArgFlagsTy PackedFlags;
    PackedFlags.setOrigAlign(CLI.DAG.getDataLayout().getABITypeAlign(
        AggregateTy));
    Type *I64Ty = Type::getInt64Ty(*CLI.DAG.getContext());
    ABIOuts.emplace_back(PackedFlags, MVT::i64, MVT::i64, I64Ty,
                         Arg.OrigArgIndex, 0);
    ABIOutVals.push_back(Packed);
    I = End;
  }

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState ArgCCInfo(CLI.CallConv, CLI.IsVarArg, MF, ArgLocs, *DAG.getContext());
  ArgCCInfo.AnalyzeCallOperands(ABIOuts, CC_MMIX);
  if (ArgLocs.size() != ABIOutVals.size())
    report_fatal_error("MMIX call assignment lost an ABI argument");
  unsigned NumBytes = ArgCCInfo.getStackSize();

  SDValue OriginalCallee = CLI.Callee;
  bool IsDirectCallee = getMMIXDirectGlobalCallee(OriginalCallee).has_value() ||
                        isa<ExternalSymbolSDNode>(OriginalCallee);
  MMIXTailCallCalleeKind CalleeKind =
      IsDirectCallee ? MMIXTailCallCalleeKind::Direct
                     : (OriginalCallee.getValueType() == MVT::i64
                            ? MMIXTailCallCalleeKind::Indirect
                            : MMIXTailCallCalleeKind::Unsupported);

  bool IsTailCall = false;
  const Function &Caller = MF.getFunction();
  bool IsCanonicalAssembly =
      getTargetMachine().getMCAsmInfo().getOutputAssemblerDialect() !=
      MMIXII::MMIXALAsmVariant;
  if (TailCallRequested &&
      isSupportedMMIXCallingConv(Caller.getCallingConv()) &&
      isSupportedMMIXCallingConv(CLI.CallConv) && IsCanonicalAssembly) {
    const Value *CallerSRet = getMMIXStructRetArgument(Caller);
    const Value *CalleeSRet = getMMIXStructRetArgument(CLI.CB);
    bool ForwardsSRet =
        CallerSRet && CalleeSRet &&
        CallerSRet->stripPointerCasts() == CalleeSRet->stripPointerCasts();

    MMIXTailCallABIInput ABI;
    ABI.CallerResult =
        classifyMMIXCallerTailResult(*this, Caller, DAG.getDataLayout());
    if (!hasCompatibleMMIXTailResultAttributes(Caller, CLI.CB))
      ABI.CallerResult = MMIXTailCallResultShape::Unsupported;
    if (CalleeSRet)
      ABI.CalleeResult = MMIXTailCallResultShape::Indirect;
    else if (CLI.OrigRetTy->isIntegerTy(128))
      ABI.CalleeResult = MMIXTailCallResultShape::TwoRegisters;
    else
      ABI.CalleeResult = classifyMMIXTailCallResultShape(
          ResultClassification.Kind, ABIIns.size());
    bool HasVectorArgument =
        llvm::any_of(CLI.Args, [&](const ArgListEntry &Arg) {
          Type *Ty = Arg.IndirectType ? Arg.IndirectType : Arg.OrigTy;
          return getMMIXFixedVectorABIWidth(Ty, DataLayout).has_value();
        });
    ABI.ArgumentsAreCompatible = !HasVectorArgument && !VectorResultWidth;
    ABI.ForwardsIndirectResult = ForwardsSRet;
    ABI.HasCallerCopy = HasCallerCopy;
    ABI.CallerCopySurvivesTransfer = false;

    MMIXTailCallFrameState Frame = MF.getSubtarget<MMIXSubtarget>()
                                       .getFrameLowering()
                                       ->analyzeTailCallFrame(MF);
    MMIXTailCallEligibilityInput Eligibility;
    Eligibility.CallerCC = Caller.getCallingConv();
    Eligibility.CalleeCC = CLI.CallConv;
    Eligibility.CallerIsVarArg = Caller.isVarArg();
    Eligibility.CalleeIsVarArg = CLI.IsVarArg;
    Eligibility.Callee = CalleeKind;
    Eligibility.HasDynamicStack = Frame.HasDynamicStack;
    Eligibility.RequiresStackRealignment = Frame.RequiresStackRealignment;
    Eligibility.CanRestoreFrame = Frame.CanRestoreFrame;
    Eligibility.OutgoingStackBytes = NumBytes;
    Eligibility.ReusableIncomingStackBytes =
        MF.getInfo<MMIXMachineFunctionInfo>()->getIncomingStackArgSize();
    applyMMIXTailCallABI(Eligibility, ABI);
    MMIXTailCallEligibility TailCallEligibility =
        classifyMMIXTailCall(Eligibility);
    switch (TailCallEligibility.getDisposition(
        IsRequiredTailCall ? MMIXTailCallRequestKind::Required
                           : MMIXTailCallRequestKind::Ordinary)) {
    case MMIXTailCallDisposition::TailTransfer:
      IsTailCall = true;
      break;
    case MMIXTailCallDisposition::NormalCall:
      break;
    case MMIXTailCallDisposition::Diagnose:
      reportFatalUsageError(
          Twine("MMIX required tail call is ineligible in function '") +
          MF.getName() + "': " + TailCallEligibility.getReasonText());
    }
  }
  CLI.IsTailCall = IsTailCall;
  if (!IsTailCall)
    Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, CLI.DL);

  SmallVector<std::pair<MCRegister, SDValue>, 16> RegsToPass;
  SmallVector<SDValue, 8> StackStores;
  SDValue StackPointer;
  for (unsigned I = 0; I != ArgLocs.size(); ++I) {
    const CCValAssign &VA = ArgLocs[I];
    SDValue Value = convertOutgoingValue(ABIOutVals[I], VA, CLI.DL, DAG);
    if (VA.isRegLoc()) {
      RegsToPass.emplace_back(VA.getLocReg(), Value);
      continue;
    }

    if (Value.getValueType() == MVT::f64)
      Value = DAG.getNode(ISD::BITCAST, CLI.DL, MVT::i64, Value);
    SDValue Address;
    MachinePointerInfo PointerInfo;
    if (IsTailCall) {
      int FI = MF.getFrameInfo().CreateFixedObject(
          /*Size=*/8, VA.getLocMemOffset(), /*IsImmutable=*/false);
      Address = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
      PointerInfo = MachinePointerInfo::getFixedStack(MF, FI);
      Chain = addTokenForMMIXTailCallArgument(Chain, DAG, MF.getFrameInfo(), FI);
    } else {
      if (!StackPointer)
        StackPointer =
            DAG.getCopyFromReg(Chain, CLI.DL, MMIX::R254, MVT::i64);
      Address = StackPointer;
      if (VA.getLocMemOffset())
        Address = DAG.getNode(
            ISD::ADD, CLI.DL, MVT::i64, StackPointer,
            DAG.getConstant(VA.getLocMemOffset(), CLI.DL, MVT::i64));
      PointerInfo = MachinePointerInfo::getStack(MF, VA.getLocMemOffset());
    }
    StackStores.push_back(
        DAG.getStore(Chain, CLI.DL, Value, Address, PointerInfo, Align(8)));
  }
  if (!StackStores.empty())
    Chain = DAG.getNode(ISD::TokenFactor, CLI.DL, MVT::Other, StackStores);

  SDValue Glue;
  copyMMIXArgumentsToRegisters(Chain, Glue, RegsToPass, CLI.DL, DAG);

  SDValue Callee = OriginalCallee;
  SDValue DirectCallee;
  if (auto GlobalCallee = getMMIXDirectGlobalCallee(Callee)) {
    const GlobalAddressSDNode *GA = GlobalCallee->first;
    SDValue Target = DAG.getTargetGlobalAddress(
        GA->getGlobal(), CLI.DL, MVT::i64, GlobalCallee->second);
    const auto *TargetFunction = dyn_cast<Function>(GA->getGlobal());
    const Function &SourceFunction = MF.getFunction();
    bool HasStableTextLayout =
        TargetFunction && !TargetFunction->isDeclaration() &&
        !getTargetMachine().getFunctionSections() &&
        !SourceFunction.hasSection() && !TargetFunction->hasSection() &&
        !SourceFunction.hasComdat() && !TargetFunction->hasComdat();
    bool IsLocalTarget =
        TargetFunction == &SourceFunction ||
        (TargetFunction &&
         (TargetFunction->hasLocalLinkage() || TargetFunction->isDSOLocal()));
    if (HasStableTextLayout && IsLocalTarget) {
      Callee = Target;
    } else {
      DirectCallee = Target;
      Callee = DAG.getNode(MMIXISD::LOAD_CALL_ADDR, CLI.DL, MVT::i64, Target);
    }
  } else if (auto *ES = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    DirectCallee = DAG.getTargetExternalSymbol(ES->getSymbol(), MVT::i64);
    Callee =
        DAG.getNode(MMIXISD::LOAD_CALL_ADDR, CLI.DL, MVT::i64, DirectCallee);
  }

  SmallVector<SDValue, 20> CallOps = {Chain};
  if (DirectCallee)
    CallOps.append({DirectCallee, Callee});
  else
    CallOps.push_back(Callee);
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  const uint32_t *Mask = TRI->getCallPreservedMask(MF, CLI.CallConv);
  if (!Mask)
    report_fatal_error("MMIX has no call-preserved mask for this convention");
  CallOps.push_back(DAG.getRegisterMask(Mask));
  for (const auto &[Reg, Value] : RegsToPass)
    CallOps.push_back(DAG.getRegister(Reg, Value.getValueType()));
  if (Glue)
    CallOps.push_back(Glue);

  unsigned CallOpcode;
  if (IsTailCall)
    CallOpcode = IsDirectCallee ? MMIXISD::DIRECT_TAIL : MMIXISD::INDIRECT_TAIL;
  else
    CallOpcode = DirectCallee ? MMIXISD::DIRECT_CALL : MMIXISD::CALL;
  Chain = DAG.getNode(CallOpcode, CLI.DL, DAG.getVTList(MVT::Other, MVT::Glue),
                      CallOps);
  Glue = Chain.getValue(1);
  if (IsTailCall) {
    MF.getFrameInfo().setHasTailCall();
    return Chain;
  }
  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, Glue, CLI.DL);
  Glue = Chain.getValue(1);

  SmallVector<CCValAssign, 2> ResultLocs;
  CCState ResultCCInfo(CLI.CallConv, CLI.IsVarArg, MF, ResultLocs,
                       *DAG.getContext());
  ResultCCInfo.AnalyzeCallResult(ABIIns, RetCC_MMIX);
  for (const CCValAssign &VA : ResultLocs) {
    SDValue Copy =
        DAG.getCopyFromReg(Chain, CLI.DL, VA.getLocReg(), VA.getLocVT(), Glue);
    SDValue Value = Copy;
    Chain = Copy.getValue(1);
    Glue = Copy.getValue(2);
    switch (VA.getLocInfo()) {
    case CCValAssign::Full:
      break;
    case CCValAssign::SExt:
      Value = DAG.getNode(ISD::AssertSext, CLI.DL, VA.getLocVT(), Value,
                          DAG.getValueType(VA.getValVT()));
      Value = DAG.getNode(ISD::TRUNCATE, CLI.DL, VA.getValVT(), Value);
      break;
    case CCValAssign::ZExt:
      Value = DAG.getNode(ISD::AssertZext, CLI.DL, VA.getLocVT(), Value,
                          DAG.getValueType(VA.getValVT()));
      Value = DAG.getNode(ISD::TRUNCATE, CLI.DL, VA.getValVT(), Value);
      break;
    case CCValAssign::AExt:
      Value = DAG.getNode(ISD::TRUNCATE, CLI.DL, VA.getValVT(), Value);
      break;
    case CCValAssign::BCvt:
      Value = convertMMIXCallBits(Value, VA.getValVT(), CLI.DL, DAG);
      break;
    default:
      report_fatal_error("MMIX does not support this call result conversion");
    }
    if (VectorResultWidth) {
      InVals.push_back(Value);
    } else if (ResultClassification.Kind == MMIXAggregateABIKind::DirectResult &&
        !IsWideComplexResult) {
      for (unsigned Part = 0; Part != CLI.Ins.size(); ++Part)
        InVals.push_back(unpackMMIXDirectAggregatePart(
            Value, CLI.Ins[Part], ResultPartOffsets[Part], ResultAggregateSize,
            CLI.DL, DAG));
    } else {
      InVals.push_back(Value);
    }
  }

  return Chain;
}

SDValue MMIXTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  const Function &F = DAG.getMachineFunction().getFunction();
  if (F.isPresplitCoroutine())
    reportFatalUsageError(
        Twine("MMIX does not support coroutines in function '") + F.getName() +
        "'");
  for (const Instruction &I : instructions(F)) {
    if (isa<VAArgInst>(I))
      reportFatalUsageError(
          Twine("MMIX does not support raw LLVM va_arg in function '") +
          F.getName() + "'; expand va_list traversal explicitly");
    if (const auto *Call = dyn_cast<CallBase>(&I)) {
      if (!isSupportedMMIXCallingConv(Call->getCallingConv()))
        reportFatalUsageError(Twine("MMIX supports only C and Fast calling "
                                    "conventions in function '") +
                              F.getName() + "'");
      if (const Function *Callee = Call->getCalledFunction()) {
        Intrinsic::ID ID = Callee->getIntrinsicID();
        if (isMMIXNonlocalControlIntrinsic(ID))
          reportFatalUsageError(
              Twine("MMIX does not support nonlocal control transfer in ") +
              "function '" + F.getName() + "'");
      }
    }
  }
  if (F.hasPersonalityFn()) {
    if (getTargetMachine().getExceptionModel() != ExceptionHandling::DwarfCFI)
      reportFatalUsageError(
          Twine("MMIX exception handling requires the DWARF model in ") +
          "function '" + F.getName() + "'");
    if (classifyEHPersonality(F.getPersonalityFn()) != EHPersonality::GNU_CXX)
      reportFatalUsageError(
          Twine("MMIX supports only the GNU C++ DWARF personality in function '") +
          F.getName() + "'");
  }
  if (!isSupportedMMIXCallingConv(CallConv))
    reportFatalUsageError(
        Twine(
            "MMIX supports only C and Fast calling conventions in function '") +
        F.getName() + "'");
  SmallVector<ISD::InputArg, 16> ABIIns;
  SmallVector<MMIXFormalArgMapping, 16> ArgMappings;
  const DataLayout &DataLayout = DAG.getDataLayout();
  for (unsigned I = 0; I != Ins.size();) {
    const ISD::InputArg &Arg = Ins[I];
    Type *AggregateTy = nullptr;
    if (Arg.isOrigArg()) {
      const Argument &OriginalArg = *F.getArg(Arg.getOrigArgIndex());
      if (Arg.Flags.isByVal())
        AggregateTy = OriginalArg.getParamByValType();
      else if (Arg.Flags.isSRet())
        AggregateTy = OriginalArg.getParamStructRetType();
      else
        AggregateTy = OriginalArg.getType();
    }
    if (!AggregateTy)
      AggregateTy =
          getMMIXFixedVectorFormalArgumentType(*this, F, DataLayout, I);
    if (AggregateTy && !isSupportedMMIXABIType(AggregateTy, DataLayout))
      reportUnsupportedMMIXABIType(AggregateTy, "formal arguments",
                                   F.getName());
    std::optional<unsigned> VectorWidth =
        AggregateTy ? getMMIXFixedVectorABIWidth(AggregateTy, DataLayout)
                    : std::nullopt;
    if (VectorWidth) {
      if (IsVarArg || Arg.VT != MVT::i64)
        reportFatalUsageError(
            Twine("MMIX does not support variadic or split fixed-vector formal ") +
            "arguments in function '" + F.getName() + "'");
      ISD::ArgFlagsTy PackedFlags;
      PackedFlags.setOrigAlign(DataLayout.getABITypeAlign(AggregateTy));
      Type *I64Ty = Type::getInt64Ty(*DAG.getContext());
      ABIIns.emplace_back(PackedFlags, MVT::i64, MVT::i64, I64Ty, Arg.Used,
                          Arg.getOrigArgIndex(), 0);
      MMIXFormalArgMapping &Mapping = ArgMappings.emplace_back();
      Mapping.IsFixedVector = true;
      ++I;
      continue;
    }
    MMIXAggregateABIClassification Classification = classifyMMIXABIValue(
        MMIXAggregateABIRole::Argument, Arg.Flags, DAG.getDataLayout(),
        AggregateTy);
    if (!Classification.isValid())
      reportMMIXABIClassificationError(Classification, "formal arguments",
                                       F.getName());
    if (Classification.isAggregate() &&
         Classification.Kind != MMIXAggregateABIKind::DirectArgument &&
         Classification.Kind != MMIXAggregateABIKind::CallerCopyArgument &&
         Classification.Kind != MMIXAggregateABIKind::IndirectResult)
      reportFatalUsageError(
          Twine("MMIX does not support this aggregate formal argument form "
                "in function '") +
          F.getName() + "'");

    if (Classification.Kind == MMIXAggregateABIKind::CallerCopyArgument) {
      ISD::ArgFlagsTy PointerFlags = getMMIXOrdinaryPointerArgFlags();
      Type *PointerTy = PointerType::getUnqual(*DAG.getContext());
      ABIIns.emplace_back(PointerFlags, MVT::i64, MVT::i64, PointerTy, Arg.Used,
                          Arg.getOrigArgIndex(), 0);
      MMIXFormalArgMapping &Mapping = ArgMappings.emplace_back();
      Mapping.OriginalParts.push_back(I);
      Mapping.LocalCopySize = Arg.Flags.getByValSize();
      Mapping.LocalCopyAlignment = Arg.Flags.getNonZeroByValAlign();
      ++I;
      continue;
    }

    if (Classification.Kind != MMIXAggregateABIKind::DirectArgument) {
      ABIIns.push_back(Arg);
      MMIXFormalArgMapping &Mapping = ArgMappings.emplace_back();
      Mapping.OriginalParts.push_back(I);
      ++I;
      continue;
    }

    unsigned End = I + 1;
    while (End != Ins.size() && Ins[End].isOrigArg() &&
           Ins[End].getOrigArgIndex() == Arg.getOrigArgIndex())
      ++End;
    bool Used = false;
    for (unsigned Part = I; Part != End; ++Part) {
      if (!isSupportedCallValueType(Ins[Part].VT))
        reportFatalUsageError(
            Twine("MMIX does not support non-scalar direct aggregate fields "
                  "in function '") +
            F.getName() + "'");
      Used |= Ins[Part].Used;
    }

    SmallVector<uint64_t, 4> PartOffsets =
        getMMIXAggregatePartOffsets(AggregateTy, DAG.getDataLayout());
    if (PartOffsets.size() != End - I)
      reportFatalUsageError(
          Twine("MMIX cannot map direct aggregate fields in function '") +
          F.getName() + "'");

    ISD::ArgFlagsTy PackedFlags;
    PackedFlags.setOrigAlign(
        DAG.getDataLayout().getABITypeAlign(AggregateTy));
    Type *I64Ty = Type::getInt64Ty(*DAG.getContext());
    ABIIns.emplace_back(PackedFlags, MVT::i64, MVT::i64, I64Ty, Used,
                        Arg.getOrigArgIndex(), 0);
    MMIXFormalArgMapping &Mapping = ArgMappings.emplace_back();
    for (unsigned Part = I; Part != End; ++Part)
      Mapping.OriginalParts.push_back(Part);
    Mapping.PartOffsets = std::move(PartOffsets);
    Mapping.AggregateSize =
        getMMIXAggregateSize(AggregateTy, DAG.getDataLayout());
    I = End;
  }

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(ABIIns, CC_MMIX);
  if (ArgLocs.size() != ArgMappings.size())
    report_fatal_error("MMIX formal assignment lost an ABI argument");
  MF.getInfo<MMIXMachineFunctionInfo>()->setIncomingStackArgSize(
      CCInfo.getStackSize());

  SmallVector<SDValue, 16> VarArgStores;
  if (IsVarArg) {
    static constexpr MCPhysReg ArgRegs[] = {
        MMIX::R231, MMIX::R232, MMIX::R233, MMIX::R234, MMIX::R235, MMIX::R236,
        MMIX::R237, MMIX::R238, MMIX::R239, MMIX::R240, MMIX::R241, MMIX::R242,
        MMIX::R243, MMIX::R244, MMIX::R245, MMIX::R246};
    constexpr unsigned SlotSize = 8;
    unsigned FirstRegister = CCInfo.getFirstUnallocated(ArgRegs);
    unsigned NamedStackSize = CCInfo.getStackSize();
    if (NamedStackSize % SlotSize != 0)
      report_fatal_error("MMIX variadic named arguments lost slot alignment");

    unsigned NamedSlots = FirstRegister + NamedStackSize / SlotSize;
    unsigned SaveSize = (std::size(ArgRegs) - FirstRegister) * SlotSize;
    int FirstUnnamedOffset = SaveSize != 0 ? -static_cast<int>(SaveSize)
                                           : static_cast<int>(NamedStackSize);
    int FI = MFI.CreateFixedObject(SaveSize != 0 ? SaveSize : SlotSize,
                                   FirstUnnamedOffset,
                                   /*IsImmutable=*/SaveSize == 0);
    MF.getInfo<MMIXMachineFunctionInfo>()->setVarArgsInfo(
        NamedSlots, FirstRegister, SaveSize, FI);

    if (SaveSize != 0) {
      SDValue SaveArea =
          DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
      for (unsigned I = FirstRegister; I != std::size(ArgRegs); ++I) {
        Register VReg =
            MRI.createVirtualRegister(&MMIX::GPR64CodeGenRegClass);
        MRI.addLiveIn(ArgRegs[I], VReg);
        SDValue Value = DAG.getCopyFromReg(Chain, DL, VReg, MVT::i64);
        uint64_t Offset = (I - FirstRegister) * SlotSize;
        SDValue Address = DAG.getMemBasePlusOffset(
            SaveArea, TypeSize::getFixed(Offset), DL);
        VarArgStores.push_back(DAG.getStore(
            Value.getValue(1), DL, Value, Address,
            MachinePointerInfo::getFixedStack(MF, FI, Offset), Align(8)));
      }
    }
  }

  for (unsigned I = 0; I != ArgLocs.size(); ++I) {
    const CCValAssign &VA = ArgLocs[I];
    SDValue Arg;
    if (VA.isRegLoc()) {
      Register VReg = MRI.createVirtualRegister(getRegClassFor(VA.getLocVT()));
      MRI.addLiveIn(VA.getLocReg(), VReg);
      Arg = DAG.getCopyFromReg(Chain, DL, VReg, VA.getLocVT());
    } else {
      int FI = MFI.CreateFixedObject(8, VA.getLocMemOffset(), true);
      SDValue FrameIndex = DAG.getFrameIndex(FI, MVT::i64);
      Arg = DAG.getLoad(VA.getLocVT(), DL, Chain, FrameIndex,
                        MachinePointerInfo::getFixedStack(MF, FI), Align(8));
    }

    const MMIXFormalArgMapping &Mapping = ArgMappings[I];
    if (Mapping.isFixedVector()) {
      InVals.push_back(Arg);
      continue;
    }
    if (Mapping.isDirectAggregate()) {
      for (unsigned Part = 0; Part != Mapping.OriginalParts.size(); ++Part) {
        const ISD::InputArg &Original = Ins[Mapping.OriginalParts[Part]];
        InVals.push_back(unpackMMIXDirectAggregatePart(
            Arg, Original, Mapping.PartOffsets[Part], Mapping.AggregateSize,
            DL, DAG));
      }
      continue;
    }

    switch (VA.getLocInfo()) {
    case CCValAssign::Full:
      break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::AssertSext, DL, VA.getLocVT(), Arg,
                        DAG.getValueType(VA.getValVT()));
      Arg = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::AssertZext, DL, VA.getLocVT(), Arg,
                        DAG.getValueType(VA.getValVT()));
      Arg = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Arg);
      break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Arg);
      break;
    case CCValAssign::BCvt:
      Arg = convertMMIXCallBits(Arg, VA.getValVT(), DL, DAG);
      break;
    default:
      report_fatal_error("MMIX does not support this argument extension");
    }
    if (Mapping.needsLocalCopy()) {
      int FI = MFI.CreateStackObject(Mapping.LocalCopySize,
                                     Mapping.LocalCopyAlignment,
                                     /*isSS=*/false);
      SDValue CopyAddress =
          DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
      SDValue SizeNode =
          DAG.getConstant(Mapping.LocalCopySize, DL, MVT::i64);
      Chain = DAG.getMemcpy(
          Chain, DL, CopyAddress, Arg, SizeNode, Mapping.LocalCopyAlignment,
          Mapping.LocalCopyAlignment, /*isVol=*/false,
          /*AlwaysInline=*/false, /*CI=*/nullptr, std::nullopt,
          MachinePointerInfo(), MachinePointerInfo());
      Arg = CopyAddress;
    }
    InVals.push_back(Arg);
  }

  if (!VarArgStores.empty()) {
    VarArgStores.push_back(Chain);
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, VarArgStores);
  }

  return Chain;
}

bool MMIXTargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context,
    const Type *RetTy) const {
  ISD::ArgFlagsTy ResultFlags;
  if (!Outs.empty())
    ResultFlags = Outs.front().Flags;
  MMIXAggregateABIClassification Classification = classifyMMIXABIValue(
      MMIXAggregateABIRole::Result, ResultFlags, MF.getDataLayout(),
      const_cast<Type *>(RetTy), Outs.size());
  std::optional<unsigned> VectorWidth = getMMIXFixedVectorABIWidth(
      const_cast<Type *>(RetTy), MF.getDataLayout());
  if (VectorWidth) {
    if (!isSupportedMMIXCallingConv(CallConv) || Outs.size() != 1 ||
        Outs.front().VT != MVT::i64)
      return false;
    ISD::ArgFlagsTy PackedFlags;
    PackedFlags.setOrigAlign(
        MF.getDataLayout().getABITypeAlign(const_cast<Type *>(RetTy)));
    SmallVector<ISD::OutputArg, 1> ABIOuts;
    ABIOuts.emplace_back(PackedFlags, MVT::i64, MVT::i64,
                         Type::getInt64Ty(Context), 0, 0);
    SmallVector<CCValAssign, 1> RetLocs;
    CCState CCInfo(CallConv, IsVarArg, MF, RetLocs, Context);
    return CCInfo.CheckReturn(ABIOuts, RetCC_MMIX);
  }
  if (Classification.Kind == MMIXAggregateABIKind::Empty)
    return Classification.isValid() && Outs.empty() &&
           isSupportedMMIXCallingConv(CallConv);

  if (Classification.Kind == MMIXAggregateABIKind::DirectResult) {
    // Do not let SelectionDAG silently demote an unsupported direct aggregate
    // to sret. LowerReturn owns the stable diagnostic for that ABI boundary.
    if (!Classification.isValid())
      return isSupportedMMIXCallingConv(CallConv);
    for (const ISD::OutputArg &Result : Outs)
      if (!isSupportedCallValueType(Result.VT))
        return false;
    if (getMMIXAggregatePartOffsets(const_cast<Type *>(RetTy),
                                    MF.getDataLayout())
            .size() != Outs.size())
      return false;

    SmallVector<ISD::OutputArg, 2> ABIOuts;
    if (isMMIXWideComplexResultType(const_cast<Type *>(RetTy))) {
      if (Outs.size() != 2 ||
          llvm::any_of(Outs, [](const ISD::OutputArg &Arg) {
            return Arg.VT != MVT::f64;
          }))
        return false;
      ABIOuts.append(Outs.begin(), Outs.end());
    } else {
      ISD::ArgFlagsTy PackedFlags;
      PackedFlags.setOrigAlign(
          MF.getDataLayout().getABITypeAlign(const_cast<Type *>(RetTy)));
      ABIOuts.emplace_back(PackedFlags, MVT::i64, MVT::i64,
                           Type::getInt64Ty(Context), 0, 0);
    }
    SmallVector<CCValAssign, 2> RetLocs;
    CCState CCInfo(CallConv, IsVarArg, MF, RetLocs, Context);
    return isSupportedMMIXCallingConv(CallConv) &&
           CCInfo.CheckReturn(ABIOuts, RetCC_MMIX);
  }

  bool SupportedType =
      Classification.isValid() && !Classification.isAggregate() &&
      (RetTy->isVoidTy() ||
       (RetTy->isIntegerTy() &&
        (RetTy->getIntegerBitWidth() <= 64 || RetTy->isIntegerTy(128))) ||
       (RetTy->isPointerTy() && RetTy->getPointerAddressSpace() == 0) ||
       RetTy->isFloatTy() || RetTy->isDoubleTy());
  if (!isSupportedMMIXCallingConv(CallConv))
    return false;
  // Claim unsupported results so LowerReturn owns the target diagnostic
  // instead of allowing SelectionDAG to silently demote them to sret.
  if (Outs.size() > (RetTy->isIntegerTy(128) ? 2u : 1u) || !SupportedType)
    return true;

  SmallVector<CCValAssign, 2> RetLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RetLocs, Context);
  return CCInfo.CheckReturn(Outs, RetCC_MMIX);
}

SDValue
MMIXTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                bool IsVarArg,
                                const SmallVectorImpl<ISD::OutputArg> &Outs,
                                const SmallVectorImpl<SDValue> &OutVals,
                                const SDLoc &DL, SelectionDAG &DAG) const {
  const Function &F = DAG.getMachineFunction().getFunction();
  if (!isSupportedMMIXCallingConv(CallConv))
    reportFatalUsageError(
        Twine(
            "MMIX supports only C and Fast calling conventions in function '") +
        F.getName() + "'");

  const DataLayout &DataLayout = DAG.getDataLayout();
  std::optional<unsigned> VectorWidth =
      getMMIXFixedVectorABIWidth(F.getReturnType(), DataLayout);
  if (!isSupportedMMIXABIType(F.getReturnType(), DataLayout))
    reportUnsupportedMMIXABIType(F.getReturnType(), "function results",
                                 F.getName());

  if (Outs.size() != OutVals.size())
    report_fatal_error("MMIX return lowering received mismatched values");

  ISD::ArgFlagsTy ResultFlags;
  if (!Outs.empty())
    ResultFlags = Outs.front().Flags;
  MMIXAggregateABIClassification Classification =
      classifyMMIXABIValue(MMIXAggregateABIRole::Result, ResultFlags,
                           DAG.getDataLayout(), F.getReturnType(), Outs.size());
  if (!Classification.isValid())
    reportMMIXABIClassificationError(Classification, "function results",
                                     F.getName());
  if (Classification.isAggregate() &&
      Classification.Kind != MMIXAggregateABIKind::Empty &&
      Classification.Kind != MMIXAggregateABIKind::DirectResult)
    reportFatalUsageError(
        Twine("MMIX does not support this aggregate function result form in ") +
        "function '" + F.getName() + "'");

  SmallVector<ISD::OutputArg, 2> ABIOuts;
  SmallVector<SDValue, 2> ABIOutVals;
  if (VectorWidth) {
    if (Outs.size() != 1 || Outs.front().VT != MVT::i64)
      reportFatalUsageError(
          Twine("MMIX cannot lower this fixed-vector function result in '") +
          F.getName() + "'");
    ABIOutVals.push_back(OutVals.front());
    ISD::ArgFlagsTy PackedFlags;
    PackedFlags.setOrigAlign(DataLayout.getABITypeAlign(F.getReturnType()));
    Type *I64Ty = Type::getInt64Ty(*DAG.getContext());
    ABIOuts.emplace_back(PackedFlags, MVT::i64, MVT::i64, I64Ty, 0, 0);
  } else if (Classification.Kind == MMIXAggregateABIKind::DirectResult) {
    SmallVector<uint64_t, 4> PartOffsets =
        getMMIXAggregatePartOffsets(F.getReturnType(), DAG.getDataLayout());
    if (PartOffsets.size() != Outs.size())
      reportFatalUsageError(
          Twine(
              "MMIX cannot map direct aggregate result fields in function '") +
          F.getName() + "'");
    if (isMMIXWideComplexResultType(F.getReturnType())) {
      if (Outs.size() != 2 ||
          llvm::any_of(Outs, [](const ISD::OutputArg &Arg) {
            return Arg.VT != MVT::f64;
          }))
        reportFatalUsageError(
            Twine("MMIX cannot lower this wide complex function result in ") +
            "function '" + F.getName() + "'");
      ABIOuts.append(Outs.begin(), Outs.end());
      ABIOutVals.append(OutVals.begin(), OutVals.end());
    } else {
      uint64_t AggregateSize =
          getMMIXAggregateSize(F.getReturnType(), DAG.getDataLayout());
      ABIOutVals.push_back(packMMIXDirectAggregate(
          Outs, OutVals, PartOffsets, AggregateSize, DL, DAG));

      ISD::ArgFlagsTy PackedFlags;
      PackedFlags.setOrigAlign(
          DAG.getDataLayout().getABITypeAlign(F.getReturnType()));
      Type *I64Ty = Type::getInt64Ty(*DAG.getContext());
      ABIOuts.emplace_back(PackedFlags, MVT::i64, MVT::i64, I64Ty, 0, 0);
    }
  } else if (Classification.Kind == MMIXAggregateABIKind::Empty) {
    if (!Outs.empty())
      report_fatal_error(
          "MMIX empty aggregate function result has value parts");
  } else {
    ABIOuts.append(Outs.begin(), Outs.end());
    ABIOutVals.append(OutVals.begin(), OutVals.end());
  }

  SmallVector<CCValAssign, 2> RetLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RetLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeReturn(ABIOuts, RetCC_MMIX);

  SDValue Glue;
  SmallVector<SDValue, 2> RetOps(1, Chain);
  for (unsigned I = 0; I != RetLocs.size(); ++I) {
    const CCValAssign &VA = RetLocs[I];
    SDValue Val = ABIOutVals[I];
    switch (VA.getLocInfo()) {
    case CCValAssign::Full:
      break;
    case CCValAssign::SExt:
      Val = DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), Val);
      break;
    case CCValAssign::ZExt:
      Val = DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), Val);
      break;
    case CCValAssign::AExt:
      Val = DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), Val);
      break;
    case CCValAssign::BCvt:
      Val = convertMMIXCallBits(Val, VA.getLocVT(), DL, DAG);
      break;
    default:
      report_fatal_error("MMIX does not support this return extension");
    }

    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), Val, Glue);
    Glue = Chain.getValue(1);
  }

  if (F.hasStructRetAttr()) {
    MachineRegisterInfo &MRI = DAG.getMachineFunction().getRegInfo();
    Register SRetVReg = MRI.getLiveInVirtReg(MMIX::R251);
    if (!SRetVReg)
      report_fatal_error("MMIX sret function has no $251 live-in");
    SDValue SRetAddress = DAG.getCopyFromReg(Chain, DL, SRetVReg, MVT::i64);
    Chain = SRetAddress.getValue(1);
    Chain = DAG.getCopyToReg(Chain, DL, MMIX::R231, SRetAddress, Glue);
    Glue = Chain.getValue(1);
  }

  RetOps[0] = Chain;
  if (Glue)
    RetOps.push_back(Glue);
  unsigned Opcode;
  if (RetLocs.size() == 2)
    Opcode = MMIXISD::RET_PAIR_GLUE;
  else if (RetLocs.empty() && !F.hasStructRetAttr())
    Opcode = MMIXISD::RET_GLUE;
  else
    Opcode = MMIXISD::RET_VALUE_GLUE;
  return DAG.getNode(Opcode, DL, MVT::Other, RetOps);
}
