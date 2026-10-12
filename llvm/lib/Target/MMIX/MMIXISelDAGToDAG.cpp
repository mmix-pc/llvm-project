//===-- MMIXISelDAGToDAG.cpp - MMIX DAG instruction selection -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MMIXMCTargetDesc.h"
#include "MMIX.h"
#include "MMIXISelLowering.h"
#include "MMIXTargetMachine.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

#define DEBUG_TYPE "mmix-isel"
#define PASS_NAME "MMIX DAG->DAG Pattern Instruction Selection"

namespace {

class MMIXDAGToDAGISel final : public SelectionDAGISel {
public:
  explicit MMIXDAGToDAGISel(MMIXTargetMachine &TM) : SelectionDAGISel(TM) {}

private:
// Include the TableGen pattern matcher. Family-specific patterns are added by
// the lowering tasks that own their semantics.
#include "MMIXGenDAGISel.inc"

  void selectSpecialArithmetic(SDNode *Node) {
    SDLoc DL(Node);
    bool IsMultiply = Node->getOpcode() == MMIXISD::UMUL_LOHI;
    bool IsUnsignedDivide = Node->getOpcode() == MMIXISD::UDIVREM;
    SDValue RHS = Node->getOperand(1);
    bool HasImmediate = false;
    if (auto *Constant = dyn_cast<ConstantSDNode>(RHS)) {
      HasImmediate = isUInt<8>(Constant->getZExtValue());
      if (HasImmediate)
        RHS = CurDAG->getTargetConstant(Constant->getZExtValue(), DL, MVT::i64);
    }

    unsigned Opcode;
    MCRegister SpecialResult;
    if (IsMultiply) {
      Opcode = HasImmediate ? MMIX::MULUI : MMIX::MULU;
      SpecialResult = MMIX::RH;
    } else if (IsUnsignedDivide) {
      Opcode = HasImmediate ? MMIX::DIVUI : MMIX::DIVU;
      SpecialResult = MMIX::RR;
    } else {
      Opcode = HasImmediate ? MMIX::DIVI : MMIX::DIV;
      SpecialResult = MMIX::RR;
    }

    SmallVector<SDValue, 3> Ops = {Node->getOperand(0), RHS};
    if (IsUnsignedDivide) {
      // A 64-bit LLVM dividend has no high half, so rD must be zero.
      SDNode *SetRD = CurDAG->getMachineNode(MMIX::SET_RD_ZERO, DL, MVT::Glue);
      Ops.push_back(SDValue(SetRD, 0));
    }

    // Glue prevents another special-register writer from being scheduled
    // between the arithmetic instruction and a required GET.
    SDNode *Arithmetic = CurDAG->getMachineNode(
        Opcode, DL, CurDAG->getVTList(MVT::i64, MVT::Glue), Ops);

    if (!SDValue(Node, 0).use_empty())
      ReplaceUses(SDValue(Node, 0), SDValue(Arithmetic, 0));
    if (!SDValue(Node, 1).use_empty()) {
      SDValue SpecialReg = CurDAG->getRegister(SpecialResult, MVT::i64);
      SDValue GetOps[] = {SpecialReg, SDValue(Arithmetic, 1)};
      SDNode *Get = CurDAG->getMachineNode(MMIX::GET, DL, MVT::i64, GetOps);
      ReplaceUses(SDValue(Node, 1), SDValue(Get, 0));
    }
    CurDAG->RemoveDeadNode(Node);
  }

  void selectSpecialRegisterAccess(SDNode *Node) {
    SDLoc DL(Node);
    if (Node->getOpcode() == MMIXISD::GET_SPECIAL_REGISTER) {
      SDValue Ops[] = {Node->getOperand(1), Node->getOperand(0)};
      CurDAG->SelectNodeTo(Node, MMIX::GET, MVT::i64, MVT::Other, Ops);
      return;
    }

    static constexpr unsigned RegisterOpcodes[] = {
        MMIX::PUT_RB_REG,  MMIX::PUT_RD_REG,  MMIX::PUT_RE_REG,
        MMIX::PUT_RH_REG,  MMIX::PUT_RJ_REG,  MMIX::PUT_RM_REG,
        MMIX::PUT_RR_REG,  MMIX::PUT_RBB_REG, MMIX::PUT_RC_REG,
        MMIX::PUT_RN_REG,  MMIX::PUT_RO_REG,  MMIX::PUT_RS_REG,
        MMIX::PUT_RI_REG,  MMIX::PUT_RT_REG,  MMIX::PUT_RTT_REG,
        MMIX::PUT_RK_REG,  MMIX::PUT_RQ_REG,  MMIX::PUT_RU_REG,
        MMIX::PUT_RV_REG,  MMIX::PUT_RG_REG,  MMIX::PUT_RL_REG,
        MMIX::PUT_RA_REG,  MMIX::PUT_RF_REG,  MMIX::PUT_RP_REG,
        MMIX::PUT_RW_REG,  MMIX::PUT_RX_REG,  MMIX::PUT_RY_REG,
        MMIX::PUT_RZ_REG,  MMIX::PUT_RWW_REG, MMIX::PUT_RXX_REG,
        MMIX::PUT_RYY_REG, MMIX::PUT_RZZ_REG};
    static constexpr unsigned ImmediateOpcodes[] = {
        MMIX::PUT_RB_IMM,  MMIX::PUT_RD_IMM,  MMIX::PUT_RE_IMM,
        MMIX::PUT_RH_IMM,  MMIX::PUT_RJ_IMM,  MMIX::PUT_RM_IMM,
        MMIX::PUT_RR_IMM,  MMIX::PUT_RBB_IMM, MMIX::PUT_RC_IMM,
        MMIX::PUT_RN_IMM,  MMIX::PUT_RO_IMM,  MMIX::PUT_RS_IMM,
        MMIX::PUT_RI_IMM,  MMIX::PUT_RT_IMM,  MMIX::PUT_RTT_IMM,
        MMIX::PUT_RK_IMM,  MMIX::PUT_RQ_IMM,  MMIX::PUT_RU_IMM,
        MMIX::PUT_RV_IMM,  MMIX::PUT_RG_IMM,  MMIX::PUT_RL_IMM,
        MMIX::PUT_RA_IMM,  MMIX::PUT_RF_IMM,  MMIX::PUT_RP_IMM,
        MMIX::PUT_RW_IMM,  MMIX::PUT_RX_IMM,  MMIX::PUT_RY_IMM,
        MMIX::PUT_RZ_IMM,  MMIX::PUT_RWW_IMM, MMIX::PUT_RXX_IMM,
        MMIX::PUT_RYY_IMM, MMIX::PUT_RZZ_IMM};
    static_assert(sizeof(RegisterOpcodes) / sizeof(RegisterOpcodes[0]) == 32);
    static_assert(sizeof(ImmediateOpcodes) / sizeof(ImmediateOpcodes[0]) == 32);
    unsigned Selector =
        cast<ConstantSDNode>(Node->getOperand(3))->getZExtValue();
    SDValue Value = Node->getOperand(2);
    unsigned Opcode = RegisterOpcodes[Selector];
    if (auto *Constant = dyn_cast<ConstantSDNode>(Value);
        Constant && isUInt<8>(Constant->getZExtValue())) {
      Opcode = ImmediateOpcodes[Selector];
      Value = CurDAG->getTargetConstant(Constant->getZExtValue(), DL, MVT::i64);
    }
    SDValue Ops[] = {Value, Node->getOperand(0)};
    CurDAG->SelectNodeTo(Node, Opcode, MVT::Other, Ops);
  }

  bool selectAddress(SDValue Address, SDValue &Base, SDValue &Offset,
                     const SDLoc &DL) {
    bool HasImmediate = true;
    Base = Address;
    Offset = CurDAG->getTargetConstant(0, DL, MVT::i64);
    if (Address.getOpcode() == ISD::ADD) {
      Base = Address.getOperand(0);
      Offset = Address.getOperand(1);
      if (auto *Constant = dyn_cast<ConstantSDNode>(Offset);
          Constant && isUInt<8>(Constant->getZExtValue()))
        Offset =
            CurDAG->getTargetConstant(Constant->getZExtValue(), DL, MVT::i64);
      else
        HasImmediate = false;
    }
    if (auto *FrameIndex = dyn_cast<FrameIndexSDNode>(Base))
      Base = CurDAG->getTargetFrameIndex(FrameIndex->getIndex(), MVT::i64);
    return HasImmediate;
  }

  bool SelectInlineAsmMemoryOperand(const SDValue &Op,
                                    InlineAsm::ConstraintCode ConstraintID,
                                    std::vector<SDValue> &OutOps) override {
    switch (ConstraintID) {
    case InlineAsm::ConstraintCode::m:
    case InlineAsm::ConstraintCode::o:
    case InlineAsm::ConstraintCode::p: {
      SDValue Base;
      SDValue Offset;
      selectAddress(Op, Base, Offset, SDLoc(Op));
      if (isa<FrameIndexSDNode>(Base)) {
        // Inline asm has no register-offset opcode to select after frame
        // layout. Allocate each frame address before register allocation.
        SDValue Zero = CurDAG->getTargetConstant(0, SDLoc(Op), MVT::i64);
        Base = SDValue(CurDAG->getMachineNode(MMIX::ADDUI, SDLoc(Op), MVT::i64,
                                            Base, Zero),
                       0);
      }
      OutOps.push_back(Base);
      OutOps.push_back(Offset);
      return false;
    }
    case InlineAsm::ConstraintCode::v:
      // MMIX has no non-offsettable ordinary CodeGen address shape.
      return true;
    default:
      llvm_unreachable("unexpected MMIX inline assembly memory constraint");
    }
  }

  void selectCacheOperation(SDNode *Node) {
    static constexpr unsigned RegisterOpcodes[] = {
        MMIX::PRELD, MMIX::PREGO, MMIX::PREST, MMIX::SYNCD, MMIX::SYNCID};
    static constexpr unsigned ImmediateOpcodes[] = {
        MMIX::PRELDI, MMIX::PREGOI, MMIX::PRESTI, MMIX::SYNCDI, MMIX::SYNCIDI};
    static_assert(sizeof(RegisterOpcodes) / sizeof(RegisterOpcodes[0]) ==
                  MMIXISD::CacheOperationEnd);
    static_assert(sizeof(ImmediateOpcodes) / sizeof(ImmediateOpcodes[0]) ==
                  MMIXISD::CacheOperationEnd);

    SDLoc DL(Node);
    SDValue Base;
    SDValue Offset;
    bool HasImmediate = selectAddress(Node->getOperand(1), Base, Offset, DL);
    unsigned Operation =
        cast<ConstantSDNode>(Node->getOperand(3))->getZExtValue();
    unsigned Opcode =
        HasImmediate ? ImmediateOpcodes[Operation] : RegisterOpcodes[Operation];
    SDValue Span = CurDAG->getTargetConstant(
        cast<ConstantSDNode>(Node->getOperand(2))->getZExtValue(), DL,
        MVT::i64);
    SDValue Ops[] = {Span, Base, Offset, Node->getOperand(0)};
    CurDAG->SelectNodeTo(Node, Opcode, MVT::Other, Ops);
  }

  void selectUncachedMemoryOperation(SDNode *Node) {
    bool IsLoad = Node->getOpcode() == MMIXISD::UNCACHED_LOAD;
    SDLoc DL(Node);
    SDValue Base;
    SDValue Offset;
    bool HasImmediate = selectAddress(Node->getOperand(1), Base, Offset, DL);
    unsigned Opcode;
    SmallVector<SDValue, 4> Ops;
    if (IsLoad) {
      Opcode = HasImmediate ? MMIX::LDUNCI : MMIX::LDUNC;
      Ops = {Base, Offset, Node->getOperand(0)};
    } else {
      Opcode = HasImmediate ? MMIX::STUNCI : MMIX::STUNC;
      Ops = {Node->getOperand(2), Base, Offset, Node->getOperand(0)};
    }

    MachineMemOperand *MemRef = cast<MemIntrinsicSDNode>(Node)->getMemOperand();
    SDNode *Selected =
        IsLoad ? CurDAG->SelectNodeTo(Node, Opcode, MVT::i64, MVT::Other, Ops)
               : CurDAG->SelectNodeTo(Node, Opcode, MVT::Other, Ops);
    CurDAG->setNodeMemRefs(cast<MachineSDNode>(Selected), {MemRef});
  }

  void selectVirtualTranslationSearch(SDNode *Node) {
    SDLoc DL(Node);
    SDValue Base;
    SDValue Offset;
    bool HasImmediate = selectAddress(Node->getOperand(1), Base, Offset, DL);
    unsigned Opcode = HasImmediate ? MMIX::LDVTSI : MMIX::LDVTS;
    SDValue Ops[] = {Base, Offset, Node->getOperand(0)};
    CurDAG->SelectNodeTo(Node, Opcode, MVT::i64, MVT::Other, Ops);
  }

  void Select(SDNode *Node) override {
    if (Node->isMachineOpcode()) {
      Node->setNodeId(-1);
      return;
    }

    if (auto *Mem = dyn_cast<MemSDNode>(Node); Mem && Mem->getAddressSpace())
      reportFatalUsageError(
          Twine("MMIX does not support nonzero address spaces in function '") +
          CurDAG->getMachineFunction().getName() + "'");

    if (Node->getOpcode() == MMIXISD::UMUL_LOHI ||
        Node->getOpcode() == MMIXISD::SDIVREM ||
        Node->getOpcode() == MMIXISD::UDIVREM) {
      selectSpecialArithmetic(Node);
      return;
    }

    if (Node->getOpcode() == MMIXISD::GET_SPECIAL_REGISTER ||
        Node->getOpcode() == MMIXISD::PUT_SPECIAL_REGISTER) {
      selectSpecialRegisterAccess(Node);
      return;
    }

    if (Node->getOpcode() == MMIXISD::F32_TO_BITS ||
        Node->getOpcode() == MMIXISD::BITS_TO_F32) {
      SDLoc DL(Node);
      unsigned RegClassID = Node->getOpcode() == MMIXISD::F32_TO_BITS
                                ? MMIX::GPR64CodeGenRegClassID
                                : MMIX::F32BitsCodeGenRegClassID;
      SDValue RegClass = CurDAG->getTargetConstant(RegClassID, DL, MVT::i64);
      CurDAG->SelectNodeTo(Node, TargetOpcode::COPY_TO_REGCLASS,
                           Node->getValueType(0), Node->getOperand(0),
                           RegClass);
      return;
    }

    if (Node->getOpcode() == MMIXISD::CACHE_OPERATION) {
      selectCacheOperation(Node);
      return;
    }

    if (Node->getOpcode() == MMIXISD::UNCACHED_LOAD ||
        Node->getOpcode() == MMIXISD::UNCACHED_STORE) {
      selectUncachedMemoryOperation(Node);
      return;
    }

    if (Node->getOpcode() == MMIXISD::VIRTUAL_TRANSLATION_SEARCH) {
      selectVirtualTranslationSearch(Node);
      return;
    }

    if (Node->getOpcode() == MMIXISD::SYNC) {
      SDLoc DL(Node);
      SDValue Mode = CurDAG->getTargetConstant(
          cast<ConstantSDNode>(Node->getOperand(1))->getZExtValue(), DL,
          MVT::i64);
      CurDAG->SelectNodeTo(Node, MMIX::SYNC, MVT::Other, Mode,
                           Node->getOperand(0));
      return;
    }

    if (Node->getOpcode() == ISD::ATOMIC_CMP_SWAP &&
        cast<AtomicSDNode>(Node)->getAlign() < Align(8))
      report_fatal_error(
          "MMIX requires naturally aligned octabyte compare-and-swap");

    if (Node->getOpcode() == ISD::FrameIndex) {
      SDLoc DL(Node);
      SDValue FrameIndex = CurDAG->getTargetFrameIndex(
          cast<FrameIndexSDNode>(Node)->getIndex(), MVT::i64);
      SDValue Offset = CurDAG->getTargetConstant(0, DL, MVT::i64);
      CurDAG->SelectNodeTo(Node, MMIX::ADDUI, MVT::i64, FrameIndex, Offset);
      return;
    }

    if (Node->getOpcode() == MMIXISD::PIC_LITERAL ||
        Node->getOpcode() == MMIXISD::GOT_LITERAL) {
      CurDAG->SelectNodeTo(Node,
                          Node->getOpcode() == MMIXISD::PIC_LITERAL
                              ? MMIX::PIC_LITERAL
                              : MMIX::GOT_LITERAL,
                          MVT::i64, Node->getOperand(0));
      return;
    }

    if (Node->getOpcode() == MMIXISD::CALL ||
        Node->getOpcode() == MMIXISD::DIRECT_CALL) {
      SmallVector<SDValue, 20> Ops;
      bool HasGlue =
          Node->getNumOperands() > 1 &&
          Node->getOperand(Node->getNumOperands() - 1).getValueType() ==
              MVT::Glue;
      unsigned LastDataOperand = Node->getNumOperands() - HasGlue;
      for (unsigned I = 1; I != LastDataOperand; ++I)
        Ops.push_back(Node->getOperand(I));
      Ops.push_back(Node->getOperand(0));
      if (HasGlue)
        Ops.push_back(Node->getOperand(Node->getNumOperands() - 1));
      unsigned Opcode = Node->getOpcode() == MMIXISD::DIRECT_CALL
                            ? MMIX::DIRECT_CALL_STATE
                            : MMIX::CALL_STATE;
      CurDAG->SelectNodeTo(Node, Opcode, MVT::Other, MVT::Glue, Ops);
      return;
    }

    if (Node->getOpcode() == MMIXISD::INDIRECT_TAIL ||
        Node->getOpcode() == MMIXISD::DIRECT_TAIL) {
      SmallVector<SDValue, 20> Ops;
      bool HasGlue =
          Node->getNumOperands() > 1 &&
          Node->getOperand(Node->getNumOperands() - 1).getValueType() ==
              MVT::Glue;
      unsigned LastDataOperand = Node->getNumOperands() - HasGlue;
      for (unsigned I = 1; I != LastDataOperand; ++I)
        Ops.push_back(Node->getOperand(I));
      Ops.push_back(Node->getOperand(0));
      if (HasGlue)
        Ops.push_back(Node->getOperand(Node->getNumOperands() - 1));

      unsigned Opcode = MMIX::INDIRECT_TAIL_STATE;
      if (Node->getOpcode() == MMIXISD::DIRECT_TAIL) {
        bool HasAddressScratch =
            Node->getNumOperands() > 2 &&
            Node->getOperand(2).getOpcode() != ISD::RegisterMask;
        Opcode = HasAddressScratch ? MMIX::MATERIALIZED_DIRECT_TAIL_STATE
                                   : MMIX::DIRECT_TAIL_STATE;
      }
      CurDAG->SelectNodeTo(Node, Opcode, MVT::Other, MVT::Glue, Ops);
      return;
    }

    if (Node->getOpcode() == MMIXISD::RET_GLUE) {
      CurDAG->SelectNodeTo(Node, MMIX::RET, MVT::Other, Node->getOperand(0));
      return;
    }

    if (Node->getOpcode() == MMIXISD::RET_VALUE_GLUE) {
      CurDAG->SelectNodeTo(Node, MMIX::RET_VALUE, MVT::Other,
                           Node->getOperand(0), Node->getOperand(1));
      return;
    }

    if (Node->getOpcode() == MMIXISD::RET_PAIR_GLUE) {
      CurDAG->SelectNodeTo(Node, MMIX::RET_PAIR, MVT::Other,
                           Node->getOperand(0), Node->getOperand(1));
      return;
    }

    SelectCode(Node);
  }
};

class MMIXDAGToDAGISelLegacy final : public SelectionDAGISelLegacy {
public:
  static char ID;

  explicit MMIXDAGToDAGISelLegacy(MMIXTargetMachine &TM)
      : SelectionDAGISelLegacy(ID, std::make_unique<MMIXDAGToDAGISel>(TM)) {}
};

} // namespace

char MMIXDAGToDAGISelLegacy::ID = 0;

INITIALIZE_PASS(MMIXDAGToDAGISelLegacy, DEBUG_TYPE, PASS_NAME, false, false)

FunctionPass *llvm::createMMIXISelDag(MMIXTargetMachine &TM) {
  return new MMIXDAGToDAGISelLegacy(TM);
}
