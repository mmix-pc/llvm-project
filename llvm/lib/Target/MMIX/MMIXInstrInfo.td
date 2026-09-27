//===-- MMIXInstrInfo.td - MMIX Instruction Definitions ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

def SDT_MMIXCallSeqStart
    : SDCallSeqStart<[SDTCisVT<0, i64>, SDTCisVT<1, i64>]>;
def SDT_MMIXCallSeqEnd
    : SDCallSeqEnd<[SDTCisVT<0, i64>, SDTCisVT<1, i64>]>;
def MMIXcallseq_start
    : SDNode<"ISD::CALLSEQ_START", SDT_MMIXCallSeqStart,
             [SDNPHasChain, SDNPOutGlue]>;
def MMIXcallseq_end
    : SDNode<"ISD::CALLSEQ_END", SDT_MMIXCallSeqEnd,
             [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;
def SDT_MMIXCall : SDTypeProfile<0, -1, [SDTCisVT<0, i64>]>;
def MMIXcall
    : SDNode<"MMIXISD::CALL", SDT_MMIXCall,
             [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue, SDNPVariadic]>;
def MMIXdirect_call
    : SDNode<"MMIXISD::DIRECT_CALL", SDT_MMIXCall,
             [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue, SDNPVariadic]>;
def MMIXindirect_tail
    : SDNode<"MMIXISD::INDIRECT_TAIL", SDT_MMIXCall,
             [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue, SDNPVariadic]>;
def MMIXdirect_tail
    : SDNode<"MMIXISD::DIRECT_TAIL", SDT_MMIXCall,
             [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue, SDNPVariadic]>;
def MMIXret
    : SDNode<"MMIXISD::RET_GLUE", SDTNone, [SDNPHasChain]>;
def MMIXret_value
    : SDNode<"MMIXISD::RET_VALUE_GLUE", SDTNone,
             [SDNPHasChain, SDNPInGlue]>;
def MMIXret_pair
    : SDNode<"MMIXISD::RET_PAIR_GLUE", SDTNone,
             [SDNPHasChain, SDNPInGlue]>;
def SDT_MMIXLoadAddr : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>]>;
class MMIXAddressProducerNode<string Use, string Opcode>
    : SDNode<Opcode, SDT_MMIXLoadAddr> {
  string MMIXAddressProducerUse = Use;
}
def MMIXload_addr
    : MMIXAddressProducerNode<"static-address", "MMIXISD::LOAD_ADDR">;
def MMIXload_call_addr
    : MMIXAddressProducerNode<"unresolved-direct-call",
                              "MMIXISD::LOAD_CALL_ADDR">;
def SDT_MMIXFPCompare
    : SDTypeProfile<1, 2, [SDTCisVT<0, i64>, SDTCisVT<1, f64>,
                           SDTCisSameAs<1, 2>]>;
def MMIXfcmp : SDNode<"MMIXISD::FCMP", SDT_MMIXFPCompare>;
def MMIXfeql : SDNode<"MMIXISD::FEQL", SDT_MMIXFPCompare>;
def MMIXfun : SDNode<"MMIXISD::FUN", SDT_MMIXFPCompare>;
def SDT_MMIXFloatFromInt
    : SDTypeProfile<1, 1, [SDTCisVT<0, f64>, SDTCisVT<1, i64>]>;
def MMIXflot : SDNode<"MMIXISD::FLOT", SDT_MMIXFloatFromInt>;
def MMIXflotu : SDNode<"MMIXISD::FLOTU", SDT_MMIXFloatFromInt>;
def MMIXsflot : SDNode<"MMIXISD::SFLOT", SDT_MMIXFloatFromInt>;
def MMIXsflotu : SDNode<"MMIXISD::SFLOTU", SDT_MMIXFloatFromInt>;
def SDT_MMIXIntFromFloat
    : SDTypeProfile<1, 1, [SDTCisVT<0, i64>, SDTCisVT<1, f64>]>;
def MMIXfixu : SDNode<"MMIXISD::FIXU", SDT_MMIXIntFromFloat>;
def SDT_MMIXF32ToBits
    : SDTypeProfile<1, 1, [SDTCisVT<0, i64>, SDTCisVT<1, f32>]>;
def SDT_MMIXBitsToF32
    : SDTypeProfile<1, 1, [SDTCisVT<0, f32>, SDTCisVT<1, i64>]>;
def MMIXf32_to_bits : SDNode<"MMIXISD::F32_TO_BITS", SDT_MMIXF32ToBits>;
def MMIXbits_to_f32 : SDNode<"MMIXISD::BITS_TO_F32", SDT_MMIXBitsToF32>;

// Common operand categories. These records carry the architectural encoding
// constraints used by later MC emitters and parsers, without installing any
// target-specific parser or encoder methods yet.
class MMIXRegisterOperand<RegisterClassLike RegClass, int Width>
    : RegisterOperand<RegClass> {
  int MMIXEncodingWidth = Width;
  bit MMIXEncodingSigned = false;
  string MMIXEncodingKind = "register";
}

def GPR64Op : MMIXRegisterOperand<GPR64, 8>;
def FPR64Op : MMIXRegisterOperand<FPR64, 8>;
def SPR64Op : MMIXRegisterOperand<SPR64, 5>;

class MMIXImmediateOperand<int Width, bit Signed, string Range>
    : Operand<i64> {
  int MMIXEncodingWidth = Width;
  bit MMIXEncodingSigned = Signed;
  string MMIXEncodingKind = "immediate";
  string MMIXEncodingRange = Range;
}

class MMIXImmediateParserOperand<string OperandName> : AsmOperandClass {
  let Name = OperandName;
  let RenderMethod = "addImmOperands";
}

def MMIXImm64AsmOperand : MMIXImmediateParserOperand<"MMIXImm64">;
def MMIXUImm8AsmOperand : MMIXImmediateParserOperand<"MMIXUImm8">;
def MMIXUImm16AsmOperand : MMIXImmediateParserOperand<"MMIXUImm16">;
def MMIXUImm24AsmOperand : MMIXImmediateParserOperand<"MMIXUImm24">;
def MMIXRoundingModeAsmOperand
    : MMIXImmediateParserOperand<"MMIXRoundingMode">;
def MMIXResumeModeAsmOperand
    : MMIXImmediateParserOperand<"MMIXResumeMode">;
def MMIXSyncModeAsmOperand
    : MMIXImmediateParserOperand<"MMIXSyncMode">;

def MMIXImm64 : MMIXImmediateOperand<64, true, "signed-64"> {
  let ParserMatchClass = MMIXImm64AsmOperand;
}
def MMIXUImm8 : MMIXImmediateOperand<8, false, "unsigned-8"> {
  let ParserMatchClass = MMIXUImm8AsmOperand;
  let OperandNamespace = "MMIXII";
  let OperandType = "OPERAND_UIMM8";
}
def MMIXUImm16 : MMIXImmediateOperand<16, false, "unsigned-16"> {
  let ParserMatchClass = MMIXUImm16AsmOperand;
  let OperandNamespace = "MMIXII";
  let OperandType = "OPERAND_UIMM16";
}
def MMIXUImm24 : MMIXImmediateOperand<24, false, "unsigned-24"> {
  let ParserMatchClass = MMIXUImm24AsmOperand;
}
def MMIXRoundingMode : MMIXImmediateOperand<3, false, "rounding-mode"> {
  let ParserMatchClass = MMIXRoundingModeAsmOperand;
  let DecoderMethod = "decodeRoundingMode";
  let OperandNamespace = "MMIXII";
  let OperandType = "OPERAND_ROUNDING_MODE";
}
def MMIXResumeMode : MMIXImmediateOperand<1, false, "resume-mode"> {
  let ParserMatchClass = MMIXResumeModeAsmOperand;
  let OperandNamespace = "MMIXII";
  let OperandType = "OPERAND_RESUME_MODE";
}
def MMIXSyncMode : MMIXImmediateOperand<3, false, "sync-mode"> {
  let ParserMatchClass = MMIXSyncModeAsmOperand;
  let OperandNamespace = "MMIXII";
  let OperandType = "OPERAND_SYNC_MODE";
}

class MMIXRegOrImmOperand<int Width, string Range>
    : Operand<i64> {
  int MMIXEncodingWidth = Width;
  bit MMIXEncodingSigned = false;
  string MMIXEncodingKind = "register-or-immediate";
  string MMIXEncodingRange = Range;
}

def MMIXRegOrImm8AsmOperand
    : MMIXImmediateParserOperand<"MMIXRegOrImm8"> {
  let RenderMethod = "addMMIXRegOrImm8Operands";
}
def MMIXRegOrImm8 : MMIXRegOrImmOperand<8, "unsigned-8"> {
  let ParserMatchClass = MMIXRegOrImm8AsmOperand;
  let OperandNamespace = "MMIXII";
  let OperandType = "OPERAND_REG_OR_IMM8";
}

class MMIXPCRelativeOperand<int Width, string Range> : Operand<OtherVT> {
  int MMIXEncodingWidth = Width;
  bit MMIXEncodingSigned = true;
  string MMIXEncodingKind = "pc-relative";
  string MMIXEncodingRange = Range;
  string MMIXFixupKind = "pc-relative";
  int MMIXFixupScale = 4;
  let EncoderMethod = "getPCRelativeOpValue";
  let DecoderMethod = "decodePCRelativeOperand";
  let OperandType = "OPERAND_PCREL";
}

def MMIXBranchTargetAsmOperand
    : MMIXImmediateParserOperand<"MMIXBranchTarget">;
def MMIXJumpTargetAsmOperand : MMIXImmediateParserOperand<"MMIXJumpTarget">;
def MMIXBranchTarget : MMIXPCRelativeOperand<16,
                                             "signed-16-instruction-words"> {
  let ParserMatchClass = MMIXBranchTargetAsmOperand;
}
def MMIXJumpTarget : MMIXPCRelativeOperand<24,
                                           "signed-24-instruction-words"> {
  let ParserMatchClass = MMIXJumpTargetAsmOperand;
}

class MMIXMemoryOffsetOperand<int Width, bit Signed, string Range>
    : Operand<i64> {
  int MMIXEncodingWidth = Width;
  bit MMIXEncodingSigned = Signed;
  string MMIXEncodingKind = "memory-offset";
  string MMIXEncodingRange = Range;
}

def MMIXMemOffset8AsmOperand
    : MMIXImmediateParserOperand<"MMIXMemOffset8">;
def MMIXMemOffset8 : MMIXMemoryOffsetOperand<8, false, "unsigned-8"> {
  let ParserMatchClass = MMIXMemOffset8AsmOperand;
  let OperandNamespace = "MMIXII";
  let OperandType = "OPERAND_UIMM8";
}

def MMIXMemAddrAsmOperand : MMIXImmediateParserOperand<"MMIXMemAddr">;
def MMIXMemAddr : Operand<i64> {
  int MMIXEncodingWidth = 8;
  bit MMIXEncodingSigned = false;
  string MMIXEncodingKind = "memory-address";
  string MMIXEncodingRange = "unsigned-8-offset";
  let ParserMatchClass = MMIXMemAddrAsmOperand;
  let OperandType = "OPERAND_MEMORY";
}

// MMIX instructions are fixed-width 32-bit words stored in big-endian byte
// order. In TableGen bit numbering, the first byte is Inst{31-24}, followed by
// the X, Y, and Z bytes. Concrete instruction formats bind every operand and
// fixed field below so the same Inst description can drive generated encoders
// and decoders.
class MMIXInstruction<dag outs, dag ins, string asmstr> : Instruction {
  let Namespace = "MMIX";
  let OutOperandList = outs;
  let InOperandList = ins;
  let AsmString = asmstr;
  let Size = 4;
  bits<32> Inst;
  bits<8> MMIXOpcode = 0;
  bits<8> MMIXX;
  bits<8> MMIXY;
  bits<8> MMIXZ;
  bit MMIXPCRelativeBackward = false;
  bits<5> MMIXPCRelativeWidth = 0;
  let Inst{31-24} = MMIXOpcode;
  let Inst{23-16} = MMIXX;
  let Inst{15-8} = MMIXY;
  let Inst{7-0} = MMIXZ;
  let TSFlags{7-0} = MMIXOpcode;
  let TSFlags{8} = MMIXPCRelativeBackward;
  let TSFlags{13-9} = MMIXPCRelativeWidth;
  // These fields give inventory tests stable semantic classifications that do
  // not depend on the instruction-format inheritance hierarchy.
  string MMIXInventoryStatus = "modeled";
  string MMIXInventoryFamily = "unclassified";
  string MMIXInventoryForm = "unclassified";
  string MMIXInventoryFeature = "base";
  // The canonical mnemonic is explicit metadata rather than a value parsed
  // from AsmString, whose assembler-variant syntax may contain operands.
  string MMIXCanonicalMnemonic = "unclassified";
  // MMIXAL-compatible output uses these independently reviewed spellings and
  // selection rules. AsmString variants may share the spelling source while
  // preserving canonical LLVM syntax as variant zero.
  string MMIXALMnemonic = "unclassified";
  string MMIXALSelection = "unclassified";
  string MMIXALOperandForm = "unclassified";
  bits<3> MMIXALSelectionKind = !cond(
      !eq(MMIXALSelection, "exact"): 1,
      !eq(MMIXALSelection, "register"): 2,
      !eq(MMIXALSelection, "immediate"): 3,
      !eq(MMIXALSelection, "forward"): 4,
      !eq(MMIXALSelection, "backward"): 5,
      true: 0);
  let TSFlags{18-16} = MMIXALSelectionKind;
}

// The ordinary integer form has a register or unsigned-byte Z operand. The
// two forms remain separate records so later MC encodings can distinguish
// their opcode fields.
class MMIXIntRegReg<string Mnemonic>
    : MMIXInstruction<(outs GPR64Op:$dst),
                      (ins GPR64Op:$lhs, GPR64Op:$rhs),
                      "{" # Mnemonic # "|" # Mnemonic #
                          "} $dst, $lhs, $rhs"> {
  bits<8> dst;
  bits<8> lhs;
  bits<8> rhs;
  let MMIXX = dst;
  let MMIXY = lhs;
  let MMIXZ = rhs;
  let MMIXInventoryFamily = "integer";
  let MMIXInventoryForm = "reg-reg-reg";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "register";
  let MMIXALOperandForm = "reg-reg-reg";
}

class MMIXIntRegImm<string Mnemonic>
    : MMIXInstruction<(outs GPR64Op:$dst),
                      (ins GPR64Op:$lhs, MMIXUImm8:$rhs),
                      "{" # Mnemonic # "|" # Mnemonic #
                          "} $dst, $lhs, $rhs"> {
  bits<8> dst;
  bits<8> lhs;
  bits<8> rhs;
  let MMIXX = dst;
  let MMIXY = lhs;
  let MMIXZ = rhs;
  let MMIXInventoryFamily = "integer";
  let MMIXInventoryForm = "reg-reg-imm8";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "immediate";
  let MMIXALOperandForm = "reg-reg-imm8";
}

// NEG and NEGU use an unsigned byte in the Y field even in their register-Z
// form, unlike the ordinary X,Y,Z instruction family.
class MMIXNegReg<string Mnemonic>
    : MMIXInstruction<(outs GPR64Op:$dst),
                      (ins MMIXUImm8:$lhs, GPR64Op:$rhs),
                      "{" # Mnemonic # "|" # Mnemonic #
                          "} $dst, $lhs, $rhs"> {
  bits<8> dst;
  bits<8> lhs;
  bits<8> rhs;
  let MMIXX = dst;
  let MMIXY = lhs;
  let MMIXZ = rhs;
  let MMIXInventoryFamily = "integer";
  let MMIXInventoryForm = "reg-imm8-reg";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "register";
  let MMIXALOperandForm = "reg-imm8-reg";
}

class MMIXNegImm<string Mnemonic>
    : MMIXInstruction<(outs GPR64Op:$dst),
                      (ins MMIXUImm8:$lhs, MMIXUImm8:$rhs),
                      "{" # Mnemonic # "|" # Mnemonic #
                          "} $dst, $lhs, $rhs"> {
  bits<8> dst;
  bits<8> lhs;
  bits<8> rhs;
  let MMIXX = dst;
  let MMIXY = lhs;
  let MMIXZ = rhs;
  let MMIXInventoryFamily = "integer";
  let MMIXInventoryForm = "reg-imm8-imm8";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "immediate";
  let MMIXALOperandForm = "reg-imm8-imm8";
}

class MMIXIntConst<string Mnemonic>
    : MMIXInstruction<(outs GPR64Op:$dst),
                      (ins MMIXUImm16:$value),
                      Mnemonic # " $dst, $value"> {
  bits<8> dst;
  bits<16> value;
  let MMIXX = dst;
  let MMIXY = value{15-8};
  let MMIXZ = value{7-0};
  let MMIXInventoryFamily = "integer";
  let MMIXInventoryForm = "reg-imm16";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "exact";
  let MMIXALOperandForm = "reg-imm16";
}

class MMIXIntRegRegOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXIntRegReg<Mnemonic> {
  let MMIXOpcode = Opcode;
}

class MMIXIntRegImmOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXIntRegImm<Mnemonic> {
  let MMIXOpcode = Opcode;
}

// CS* keeps the old X register when its condition is false. Model that X
// operand as both an input and an output so register allocation preserves the
// architectural two-address dependency.
class MMIXCondSetRegOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXInstruction<(outs GPR64Op:$dst),
                      (ins GPR64Op:$old, GPR64Op:$cond, GPR64Op:$value),
                      "{" # Mnemonic # "|" # Mnemonic #
                          "} $dst, $cond, $value"> {
  bits<8> dst;
  bits<8> cond;
  bits<8> value;
  let MMIXX = dst;
  let MMIXY = cond;
  let MMIXZ = value;
  let MMIXOpcode = Opcode;
  let Constraints = "$dst = $old";
  let MMIXInventoryFamily = "integer";
  let MMIXInventoryForm = "reg-reg-reg";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "register";
  let MMIXALOperandForm = "reg-reg-reg";
}

class MMIXCondSetImmOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXInstruction<(outs GPR64Op:$dst),
                      (ins GPR64Op:$old, GPR64Op:$cond, MMIXUImm8:$value),
                      "{" # Mnemonic # "|" # Mnemonic #
                          "} $dst, $cond, $value"> {
  bits<8> dst;
  bits<8> cond;
  bits<8> value;
  let MMIXX = dst;
  let MMIXY = cond;
  let MMIXZ = value;
  let MMIXOpcode = Opcode;
  let Constraints = "$dst = $old";
  let MMIXInventoryFamily = "integer";
  let MMIXInventoryForm = "reg-reg-imm8";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "immediate";
  let MMIXALOperandForm = "reg-reg-imm8";
}

class MMIXNegRegOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXNegReg<Mnemonic> {
  let MMIXOpcode = Opcode;
}

class MMIXNegImmOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXNegImm<Mnemonic> {
  let MMIXOpcode = Opcode;
}

class MMIXIntConstOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXIntConst<Mnemonic> {
  let MMIXOpcode = Opcode;
}

// Arithmetic, shifts, comparisons, conditional sets, and bit operations all
// have the architectural register and immediate variants listed here.
def MUL : MMIXIntRegRegOpcode<0x18, "MUL">;
def MULI : MMIXIntRegImmOpcode<0x19, "MUL">;
let Defs = [RH] in {
  def MULU : MMIXIntRegRegOpcode<0x1A, "MULU">;
  def MULUI : MMIXIntRegImmOpcode<0x1B, "MULU">;
}
let Defs = [RR] in {
  def DIV : MMIXIntRegRegOpcode<0x1C, "DIV">;
  def DIVI : MMIXIntRegImmOpcode<0x1D, "DIV">;
}
let Defs = [RR], Uses = [RD] in {
  def DIVU : MMIXIntRegRegOpcode<0x1E, "DIVU">;
  def DIVUI : MMIXIntRegImmOpcode<0x1F, "DIVU">;
}
def ADD : MMIXIntRegRegOpcode<0x20, "ADD">;
def ADDI : MMIXIntRegImmOpcode<0x21, "ADD">;
def ADDU : MMIXIntRegRegOpcode<0x22, "ADDU">;
def ADDUI : MMIXIntRegImmOpcode<0x23, "ADDU">;
def SUB : MMIXIntRegRegOpcode<0x24, "SUB">;
def SUBI : MMIXIntRegImmOpcode<0x25, "SUB">;
def SUBU : MMIXIntRegRegOpcode<0x26, "SUBU">;
def SUBUI : MMIXIntRegImmOpcode<0x27, "SUBU">;
def CMP : MMIXIntRegRegOpcode<0x30, "CMP">;
def CMPI : MMIXIntRegImmOpcode<0x31, "CMP">;
def CMPU : MMIXIntRegRegOpcode<0x32, "CMPU">;
def CMPUI : MMIXIntRegImmOpcode<0x33, "CMPU">;
def NEG : MMIXNegRegOpcode<0x34, "NEG">;
def NEGI : MMIXNegImmOpcode<0x35, "NEG">;
def NEGU : MMIXNegRegOpcode<0x36, "NEGU">;
def NEGUI : MMIXNegImmOpcode<0x37, "NEGU">;
def SL : MMIXIntRegRegOpcode<0x38, "SL">;
def SLI : MMIXIntRegImmOpcode<0x39, "SL">;
def SLU : MMIXIntRegRegOpcode<0x3A, "SLU">;
def SLUI : MMIXIntRegImmOpcode<0x3B, "SLU">;
def SR : MMIXIntRegRegOpcode<0x3C, "SR">;
def SRI : MMIXIntRegImmOpcode<0x3D, "SR">;
def SRU : MMIXIntRegRegOpcode<0x3E, "SRU">;
def SRUI : MMIXIntRegImmOpcode<0x3F, "SRU">;

def TWOADDU : MMIXIntRegRegOpcode<0x28, "2ADDU">;
def TWOADDUI : MMIXIntRegImmOpcode<0x29, "2ADDU">;
def FOURADDU : MMIXIntRegRegOpcode<0x2A, "4ADDU">;
def FOURADDUI : MMIXIntRegImmOpcode<0x2B, "4ADDU">;
def EIGHTADDU : MMIXIntRegRegOpcode<0x2C, "8ADDU">;
def EIGHTADDUI : MMIXIntRegImmOpcode<0x2D, "8ADDU">;
def SIXTEENADDU : MMIXIntRegRegOpcode<0x2E, "16ADDU">;
def SIXTEENADDUI : MMIXIntRegImmOpcode<0x2F, "16ADDU">;

def mmix_uimm8 : PatLeaf<(imm), [{
  return isUInt<8>(N->getZExtValue());
}]>;

def mmix_neg_uimm8 : PatLeaf<(imm), [{
  int64_t Value = N->getSExtValue();
  return Value < 0 && Value >= -255;
}]>;

def mmix_neg_imm : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(-N->getSExtValue(), SDLoc(N), MVT::i64);
}]>;

def mmix_uimm6 : PatLeaf<(imm), [{
  return isUInt<6>(N->getZExtValue());
}]>;

def mmix_inv_uimm8 : PatLeaf<(imm), [{
  return isUInt<8>(~N->getZExtValue());
}]>;

def mmix_inv_imm : SDNodeXForm<imm, [{
  return CurDAG->getTargetConstant(~N->getZExtValue(), SDLoc(N), MVT::i64);
}]>;

def mmix_fpimm_bits : SDNodeXForm<fpimm, [{
  return CurDAG->getTargetConstant(
      N->getValueAPF().bitcastToAPInt().getZExtValue(), SDLoc(N), MVT::i64);
}]>;

// LLVM integer arithmetic wraps modulo 2^64 unless overflow makes the result
// poison. Neither case permits the traps raised by MMIX ADD, SUB, and NEG, so
// instruction selection exclusively uses their unsigned, nontrapping forms.
def : Pat<(add i64:$lhs, mmix_uimm8:$rhs),
          (ADDUI $lhs, mmix_uimm8:$rhs)>;
def : Pat<(add i64:$lhs, mmix_neg_uimm8:$rhs),
          (SUBUI $lhs, (mmix_neg_imm mmix_neg_uimm8:$rhs))>;
def : Pat<(add i64:$lhs, i64:$rhs), (ADDU $lhs, $rhs)>;

def : Pat<(sub i64:$lhs, mmix_uimm8:$rhs),
          (SUBUI $lhs, mmix_uimm8:$rhs)>;
def : Pat<(sub i64:$lhs, mmix_neg_uimm8:$rhs),
          (ADDUI $lhs, (mmix_neg_imm mmix_neg_uimm8:$rhs))>;
def : Pat<(sub 0, i64:$rhs), (NEGU 0, $rhs)>;
def : Pat<(sub i64:$lhs, i64:$rhs), (SUBU $lhs, $rhs)>;

def : Pat<(mul i64:$lhs, mmix_uimm8:$rhs),
          (MULUI $lhs, mmix_uimm8:$rhs)>;
def : Pat<(mul i64:$lhs, i64:$rhs), (MULU $lhs, $rhs)>;

// These instructions compute (scale * Y + Z) modulo 2^64, exactly matching a
// constant left shift followed by wrapping addition. The combined instruction
// is strictly cheaper than emitting SLU and ADDU separately. Shift counts
// outside 1..4 deliberately retain the generic sequence.
multiclass MMIXScaledAddPatterns<Instruction RegOp, Instruction ImmOp,
                                 int Shift> {
  def : Pat<(add (shl i64:$scaled, Shift), mmix_uimm8:$addend),
            (ImmOp $scaled, mmix_uimm8:$addend)>;
  def : Pat<(add (shl i64:$scaled, Shift), i64:$addend),
            (RegOp $scaled, $addend)>;
}

defm : MMIXScaledAddPatterns<TWOADDU, TWOADDUI, 1>;
defm : MMIXScaledAddPatterns<FOURADDU, FOURADDUI, 2>;
defm : MMIXScaledAddPatterns<EIGHTADDU, EIGHTADDUI, 3>;
defm : MMIXScaledAddPatterns<SIXTEENADDU, SIXTEENADDUI, 4>;

def CSN : MMIXCondSetRegOpcode<0x60, "CSN">;
def CSNI : MMIXCondSetImmOpcode<0x61, "CSN">;
def CSZ : MMIXCondSetRegOpcode<0x62, "CSZ">;
def CSZI : MMIXCondSetImmOpcode<0x63, "CSZ">;
def CSP : MMIXCondSetRegOpcode<0x64, "CSP">;
def CSPI : MMIXCondSetImmOpcode<0x65, "CSP">;
def CSOD : MMIXCondSetRegOpcode<0x66, "CSOD">;
def CSODI : MMIXCondSetImmOpcode<0x67, "CSOD">;
def CSNN : MMIXCondSetRegOpcode<0x68, "CSNN">;
def CSNNI : MMIXCondSetImmOpcode<0x69, "CSNN">;
def CSNZ : MMIXCondSetRegOpcode<0x6A, "CSNZ">;
def CSNZI : MMIXCondSetImmOpcode<0x6B, "CSNZ">;
def CSNP : MMIXCondSetRegOpcode<0x6C, "CSNP">;
def CSNPI : MMIXCondSetImmOpcode<0x6D, "CSNP">;
def CSEV : MMIXCondSetRegOpcode<0x6E, "CSEV">;
def CSEVI : MMIXCondSetImmOpcode<0x6F, "CSEV">;
def ZSN : MMIXIntRegRegOpcode<0x70, "ZSN">;
def ZSNI : MMIXIntRegImmOpcode<0x71, "ZSN">;
def ZSZ : MMIXIntRegRegOpcode<0x72, "ZSZ">;
def ZSZI : MMIXIntRegImmOpcode<0x73, "ZSZ">;
def ZSP : MMIXIntRegRegOpcode<0x74, "ZSP">;
def ZSPI : MMIXIntRegImmOpcode<0x75, "ZSP">;
def ZSOD : MMIXIntRegRegOpcode<0x76, "ZSOD">;
def ZSODI : MMIXIntRegImmOpcode<0x77, "ZSOD">;
def ZSNN : MMIXIntRegRegOpcode<0x78, "ZSNN">;
def ZSNNI : MMIXIntRegImmOpcode<0x79, "ZSNN">;
def ZSNZ : MMIXIntRegRegOpcode<0x7A, "ZSNZ">;
def ZSNZI : MMIXIntRegImmOpcode<0x7B, "ZSNZ">;
def ZSNP : MMIXIntRegRegOpcode<0x7C, "ZSNP">;
def ZSNPI : MMIXIntRegImmOpcode<0x7D, "ZSNP">;
def ZSEV : MMIXIntRegRegOpcode<0x7E, "ZSEV">;
def ZSEVI : MMIXIntRegImmOpcode<0x7F, "ZSEV">;

// CMP and CMPU produce -1, 0, or 1. A zero-set instruction then maps the
// requested relation to LLVM's canonical 0-or-1 Boolean representation.
def : Pat<(setlt i64:$lhs, i64:$rhs), (ZSNI (CMP $lhs, $rhs), 1)>;
def : Pat<(setle i64:$lhs, i64:$rhs), (ZSNPI (CMP $lhs, $rhs), 1)>;
def : Pat<(setgt i64:$lhs, i64:$rhs), (ZSPI (CMP $lhs, $rhs), 1)>;
def : Pat<(setge i64:$lhs, i64:$rhs), (ZSNNI (CMP $lhs, $rhs), 1)>;
def : Pat<(setult i64:$lhs, i64:$rhs), (ZSNI (CMPU $lhs, $rhs), 1)>;
def : Pat<(setule i64:$lhs, i64:$rhs), (ZSNPI (CMPU $lhs, $rhs), 1)>;
def : Pat<(setugt i64:$lhs, i64:$rhs), (ZSPI (CMPU $lhs, $rhs), 1)>;
def : Pat<(setuge i64:$lhs, i64:$rhs), (ZSNNI (CMPU $lhs, $rhs), 1)>;
def : Pat<(seteq i64:$lhs, i64:$rhs), (ZSZI (CMPU $lhs, $rhs), 1)>;
def : Pat<(setne i64:$lhs, i64:$rhs), (ZSNZI (CMPU $lhs, $rhs), 1)>;

def : Pat<(setlt i64:$lhs, mmix_uimm8:$rhs),
          (ZSNI (CMPI $lhs, mmix_uimm8:$rhs), 1)>;
def : Pat<(setle i64:$lhs, mmix_uimm8:$rhs),
          (ZSNPI (CMPI $lhs, mmix_uimm8:$rhs), 1)>;
def : Pat<(setgt i64:$lhs, mmix_uimm8:$rhs),
          (ZSPI (CMPI $lhs, mmix_uimm8:$rhs), 1)>;
def : Pat<(setge i64:$lhs, mmix_uimm8:$rhs),
          (ZSNNI (CMPI $lhs, mmix_uimm8:$rhs), 1)>;
def : Pat<(setult i64:$lhs, mmix_uimm8:$rhs),
          (ZSNI (CMPUI $lhs, mmix_uimm8:$rhs), 1)>;
def : Pat<(setule i64:$lhs, mmix_uimm8:$rhs),
          (ZSNPI (CMPUI $lhs, mmix_uimm8:$rhs), 1)>;
def : Pat<(setugt i64:$lhs, mmix_uimm8:$rhs),
          (ZSPI (CMPUI $lhs, mmix_uimm8:$rhs), 1)>;
def : Pat<(setuge i64:$lhs, mmix_uimm8:$rhs),
          (ZSNNI (CMPUI $lhs, mmix_uimm8:$rhs), 1)>;
def : Pat<(seteq i64:$lhs, mmix_uimm8:$rhs),
          (ZSZI (CMPUI $lhs, mmix_uimm8:$rhs), 1)>;
def : Pat<(setne i64:$lhs, mmix_uimm8:$rhs),
          (ZSNZI (CMPUI $lhs, mmix_uimm8:$rhs), 1)>;

// CSNZ selects its value for every true LLVM condition. When the false value
// is encodable instead, CSZ reverses the choice and keeps the true register in
// the tied destination.
def : Pat<(select i64:$cond, i64:$true, i64:$false),
          (CSNZ $false, $cond, $true)>;
def : Pat<(select i64:$cond, mmix_uimm8:$true, i64:$false),
          (CSNZI $false, $cond, mmix_uimm8:$true)>;
def : Pat<(select i64:$cond, i64:$true, mmix_uimm8:$false),
          (CSZI $true, $cond, mmix_uimm8:$false)>;

def OR : MMIXIntRegRegOpcode<0xC0, "OR">;
def ORI : MMIXIntRegImmOpcode<0xC1, "OR">;
def ORN : MMIXIntRegRegOpcode<0xC2, "ORN">;
def ORNI : MMIXIntRegImmOpcode<0xC3, "ORN">;
def NOR : MMIXIntRegRegOpcode<0xC4, "NOR">;
def NORI : MMIXIntRegImmOpcode<0xC5, "NOR">;
def XOR : MMIXIntRegRegOpcode<0xC6, "XOR">;
def XORI : MMIXIntRegImmOpcode<0xC7, "XOR">;
def AND : MMIXIntRegRegOpcode<0xC8, "AND">;
def ANDI : MMIXIntRegImmOpcode<0xC9, "AND">;
def ANDN : MMIXIntRegRegOpcode<0xCA, "ANDN">;
def ANDNI : MMIXIntRegImmOpcode<0xCB, "ANDN">;
def NAND : MMIXIntRegRegOpcode<0xCC, "NAND">;
def NANDI : MMIXIntRegImmOpcode<0xCD, "NAND">;
def NXOR : MMIXIntRegRegOpcode<0xCE, "NXOR">;
def NXORI : MMIXIntRegImmOpcode<0xCF, "NXOR">;

// SLU is the nontrapping left shift required by LLVM's poison-on-overflow
// semantics. MMIX register shifts are exact for every non-poison LLVM shift
// count; immediate forms are restricted to that valid 0..63 range.
def : Pat<(shl i64:$value, mmix_uimm6:$amount),
          (SLUI $value, mmix_uimm6:$amount)>;
def : Pat<(shl i64:$value, i64:$amount), (SLU $value, $amount)>;
def : Pat<(sra i64:$value, mmix_uimm6:$amount),
          (SRI $value, mmix_uimm6:$amount)>;
def : Pat<(sra i64:$value, i64:$amount), (SR $value, $amount)>;
def : Pat<(srl i64:$value, mmix_uimm6:$amount),
          (SRUI $value, mmix_uimm6:$amount)>;
def : Pat<(srl i64:$value, i64:$amount), (SRU $value, $amount)>;

def : Pat<(sext_inreg i64:$value, i1),
          (SRI (SLUI $value, 63), 63)>;
def : Pat<(sext_inreg i64:$value, i8),
          (SRI (SLUI $value, 56), 56)>;
def : Pat<(sext_inreg i64:$value, i16),
          (SRI (SLUI $value, 48), 48)>;
def : Pat<(sext_inreg i64:$value, i32),
          (SRI (SLUI $value, 32), 32)>;

// Select complemented forms only when the complete Boolean identity is
// present. The immediate forms encode the complement in their Z byte.
def : Pat<(xor (or i64:$lhs, i64:$rhs), (i64 -1)),
          (NOR $lhs, $rhs)>;
def : Pat<(xor (or i64:$lhs, mmix_uimm8:$rhs), (i64 -1)),
          (NORI $lhs, mmix_uimm8:$rhs)>;
def : Pat<(xor (and i64:$lhs, i64:$rhs), (i64 -1)),
          (NAND $lhs, $rhs)>;
def : Pat<(xor (and i64:$lhs, mmix_uimm8:$rhs), (i64 -1)),
          (NANDI $lhs, mmix_uimm8:$rhs)>;
def : Pat<(xor (xor i64:$lhs, i64:$rhs), (i64 -1)),
          (NXOR $lhs, $rhs)>;
def : Pat<(xor (xor i64:$lhs, mmix_uimm8:$rhs), (i64 -1)),
          (NXORI $lhs, mmix_uimm8:$rhs)>;

def : Pat<(and i64:$lhs, (xor i64:$rhs, (i64 -1))),
          (ANDN $lhs, $rhs)>;
def : Pat<(and (xor i64:$rhs, (i64 -1)), i64:$lhs),
          (ANDN $lhs, $rhs)>;
def : Pat<(and (xor i64:$lhs, (i64 -1)), mmix_inv_uimm8:$rhs),
          (NORI $lhs, (mmix_inv_imm mmix_inv_uimm8:$rhs))>;
def : Pat<(and i64:$lhs, mmix_uimm8:$rhs),
          (ANDI $lhs, mmix_uimm8:$rhs)>;
def : Pat<(and i64:$lhs, mmix_inv_uimm8:$rhs),
          (ANDNI $lhs, (mmix_inv_imm mmix_inv_uimm8:$rhs))>;
def : Pat<(and i64:$lhs, i64:$rhs), (AND $lhs, $rhs)>;

def : Pat<(or i64:$lhs, (xor i64:$rhs, (i64 -1))),
          (ORN $lhs, $rhs)>;
def : Pat<(or (xor i64:$rhs, (i64 -1)), i64:$lhs),
          (ORN $lhs, $rhs)>;
def : Pat<(or (xor i64:$lhs, (i64 -1)), mmix_inv_uimm8:$rhs),
          (NANDI $lhs, (mmix_inv_imm mmix_inv_uimm8:$rhs))>;
def : Pat<(or i64:$lhs, mmix_uimm8:$rhs),
          (ORI $lhs, mmix_uimm8:$rhs)>;
def : Pat<(or i64:$lhs, mmix_inv_uimm8:$rhs),
          (ORNI $lhs, (mmix_inv_imm mmix_inv_uimm8:$rhs))>;
def : Pat<(or i64:$lhs, i64:$rhs), (OR $lhs, $rhs)>;

def : Pat<(xor i64:$lhs, (xor i64:$rhs, (i64 -1))),
          (NXOR $lhs, $rhs)>;
def : Pat<(xor (xor i64:$rhs, (i64 -1)), i64:$lhs),
          (NXOR $lhs, $rhs)>;
def : Pat<(xor i64:$lhs, mmix_uimm8:$rhs),
          (XORI $lhs, mmix_uimm8:$rhs)>;
def : Pat<(xor i64:$lhs, mmix_inv_uimm8:$rhs),
          (NXORI $lhs, (mmix_inv_imm mmix_inv_uimm8:$rhs))>;
def : Pat<(xor i64:$lhs, i64:$rhs), (XOR $lhs, $rhs)>;

// Integer and floating-point values use different virtual register classes
// but share the same physical MMIX registers, so bitcasts are class-only
// copies and emit no instruction after coalescing.
def : Pat<(f64 (bitconvert i64:$src)),
          (COPY_TO_REGCLASS GPR64:$src, FPR64)>;
def : Pat<(i64 (bitconvert f64:$src)),
          (COPY_TO_REGCLASS FPR64:$src, GPR64)>;

def BDIF : MMIXIntRegRegOpcode<0xD0, "BDIF">;
def BDIFI : MMIXIntRegImmOpcode<0xD1, "BDIF">;
def WDIF : MMIXIntRegRegOpcode<0xD2, "WDIF">;
def WDIFI : MMIXIntRegImmOpcode<0xD3, "WDIF">;
def TDIF : MMIXIntRegRegOpcode<0xD4, "TDIF">;
def TDIFI : MMIXIntRegImmOpcode<0xD5, "TDIF">;
def ODIF : MMIXIntRegRegOpcode<0xD6, "ODIF">;
def ODIFI : MMIXIntRegImmOpcode<0xD7, "ODIF">;
def MUX : MMIXIntRegRegOpcode<0xD8, "MUX">;
def MUXI : MMIXIntRegImmOpcode<0xD9, "MUX">;
def SADD : MMIXIntRegRegOpcode<0xDA, "SADD">;
def SADDI : MMIXIntRegImmOpcode<0xDB, "SADD">;
def MOR : MMIXIntRegRegOpcode<0xDC, "MOR">;
def MORI : MMIXIntRegImmOpcode<0xDD, "MOR">;
def MXOR : MMIXIntRegRegOpcode<0xDE, "MXOR">;
def MXORI : MMIXIntRegImmOpcode<0xDF, "MXOR">;

// ODIF is unsigned saturating subtraction over one 64-bit octabyte. Its
// narrower relatives operate independently on packed lanes and therefore do
// not implement scalar or generic-vector subtraction. SADD counts the bits in
// Y that are absent from Z, so only a zero second operand is generic ctpop.
def : Pat<(usubsat i64:$lhs, mmix_uimm8:$rhs),
          (ODIFI $lhs, mmix_uimm8:$rhs)>;
def : Pat<(usubsat i64:$lhs, i64:$rhs), (ODIF $lhs, $rhs)>;
def : Pat<(ctpop i64:$value), (SADDI $value, 0)>;

def SETH : MMIXIntConstOpcode<0xE0, "SETH">;
def SETMH : MMIXIntConstOpcode<0xE1, "SETMH">;
def SETML : MMIXIntConstOpcode<0xE2, "SETML">;
def SETL : MMIXIntConstOpcode<0xE3, "SETL">;
def INCH : MMIXIntConstOpcode<0xE4, "INCH">;
def INCMH : MMIXIntConstOpcode<0xE5, "INCMH">;
def INCML : MMIXIntConstOpcode<0xE6, "INCML">;
def INCL : MMIXIntConstOpcode<0xE7, "INCL">;
def ORH : MMIXIntConstOpcode<0xE8, "ORH">;
def ORMH : MMIXIntConstOpcode<0xE9, "ORMH">;
def ORML : MMIXIntConstOpcode<0xEA, "ORML">;
def ORL : MMIXIntConstOpcode<0xEB, "ORL">;
def ANDNH : MMIXIntConstOpcode<0xEC, "ANDNH">;
def ANDNMH : MMIXIntConstOpcode<0xED, "ANDNMH">;
def ANDNML : MMIXIntConstOpcode<0xEE, "ANDNML">;
def ANDNL : MMIXIntConstOpcode<0xEF, "ANDNL">;

// Integer memory operations use a register base and either a register or an
// unsigned-byte offset. The load and store records keep width and signedness
// variants distinct for the later MC and lowering work.
class MMIXLoadReg<string Mnemonic>
    : MMIXInstruction<(outs GPR64Op:$dst),
                      (ins GPR64Op:$base, GPR64Op:$offset),
                      "{" # Mnemonic # "|" # Mnemonic #
                          "} $dst, $base, $offset"> {
  bits<8> dst;
  bits<8> base;
  bits<8> offset;
  let MMIXX = dst;
  let MMIXY = base;
  let MMIXZ = offset;
  let mayLoad = true;
  let MMIXInventoryFamily = "memory";
  let MMIXInventoryForm = "reg-reg-reg";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "register";
  let MMIXALOperandForm = "reg-reg-reg";
}

class MMIXLoadImm<string Mnemonic>
    : MMIXInstruction<(outs GPR64Op:$dst),
                      (ins GPR64Op:$base, MMIXMemOffset8:$offset),
                      "{" # Mnemonic # "|" # Mnemonic #
                          "} $dst, $base, $offset"> {
  bits<8> dst;
  bits<8> base;
  bits<8> offset;
  let MMIXX = dst;
  let MMIXY = base;
  let MMIXZ = offset;
  let mayLoad = true;
  let MMIXInventoryFamily = "memory";
  let MMIXInventoryForm = "reg-reg-imm8";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "immediate";
  let MMIXALOperandForm = "reg-reg-imm8";
}

class MMIXStoreReg<string Mnemonic>
    : MMIXInstruction<(outs),
                      (ins GPR64Op:$value, GPR64Op:$base, GPR64Op:$offset),
                      "{" # Mnemonic # "|" # Mnemonic #
                          "} $value, $base, $offset"> {
  bits<8> value;
  bits<8> base;
  bits<8> offset;
  let MMIXX = value;
  let MMIXY = base;
  let MMIXZ = offset;
  let mayStore = true;
  let MMIXInventoryFamily = "memory";
  let MMIXInventoryForm = "reg-reg-reg";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "register";
  let MMIXALOperandForm = "reg-reg-reg";
}

class MMIXStoreImm<string Mnemonic>
    : MMIXInstruction<(outs),
                      (ins GPR64Op:$value, GPR64Op:$base,
                           MMIXMemOffset8:$offset),
                      "{" # Mnemonic # "|" # Mnemonic #
                          "} $value, $base, $offset"> {
  bits<8> value;
  bits<8> base;
  bits<8> offset;
  let MMIXX = value;
  let MMIXY = base;
  let MMIXZ = offset;
  let mayStore = true;
  let MMIXInventoryFamily = "memory";
  let MMIXInventoryForm = "reg-reg-imm8";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "immediate";
  let MMIXALOperandForm = "reg-reg-imm8";
}

class MMIXLoadRegOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXLoadReg<Mnemonic> {
  let MMIXOpcode = Opcode;
}

class MMIXLoadImmOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXLoadImm<Mnemonic> {
  let MMIXOpcode = Opcode;
}

class MMIXStoreRegOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXStoreReg<Mnemonic> {
  let MMIXOpcode = Opcode;
}

class MMIXStoreImmOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXStoreImm<Mnemonic> {
  let MMIXOpcode = Opcode;
}

def LDB : MMIXLoadRegOpcode<0x80, "LDB">;
def LDBI : MMIXLoadImmOpcode<0x81, "LDB">;
def LDBU : MMIXLoadRegOpcode<0x82, "LDBU">;
def LDBUI : MMIXLoadImmOpcode<0x83, "LDBU">;
def LDW : MMIXLoadRegOpcode<0x84, "LDW">;
def LDWI : MMIXLoadImmOpcode<0x85, "LDW">;
def LDWU : MMIXLoadRegOpcode<0x86, "LDWU">;
def LDWUI : MMIXLoadImmOpcode<0x87, "LDWU">;
def LDT : MMIXLoadRegOpcode<0x88, "LDT">;
def LDTI : MMIXLoadImmOpcode<0x89, "LDT">;
def LDTU : MMIXLoadRegOpcode<0x8A, "LDTU">;
def LDTUI : MMIXLoadImmOpcode<0x8B, "LDTU">;
def LDO : MMIXLoadRegOpcode<0x8C, "LDO">;
def LDOI : MMIXLoadImmOpcode<0x8D, "LDO">;
def LDOU : MMIXLoadRegOpcode<0x8E, "LDOU">;
def LDOUI : MMIXLoadImmOpcode<0x8F, "LDOU">;
def LDHT : MMIXLoadRegOpcode<0x92, "LDHT">;
def LDHTI : MMIXLoadImmOpcode<0x93, "LDHT">;

def STB : MMIXStoreRegOpcode<0xA0, "STB">;
def STBI : MMIXStoreImmOpcode<0xA1, "STB">;
def STBU : MMIXStoreRegOpcode<0xA2, "STBU">;
def STBUI : MMIXStoreImmOpcode<0xA3, "STBU">;
def STW : MMIXStoreRegOpcode<0xA4, "STW">;
def STWI : MMIXStoreImmOpcode<0xA5, "STW">;
def STWU : MMIXStoreRegOpcode<0xA6, "STWU">;
def STWUI : MMIXStoreImmOpcode<0xA7, "STWU">;
def STT : MMIXStoreRegOpcode<0xA8, "STT">;
def STTI : MMIXStoreImmOpcode<0xA9, "STT">;
def STTU : MMIXStoreRegOpcode<0xAA, "STTU">;
def STTUI : MMIXStoreImmOpcode<0xAB, "STTU">;
def STO : MMIXStoreRegOpcode<0xAC, "STO">;
def STOI : MMIXStoreImmOpcode<0xAD, "STO">;
def STOU : MMIXStoreRegOpcode<0xAE, "STOU">;
def STOUI : MMIXStoreImmOpcode<0xAF, "STOU">;
def STHT : MMIXStoreRegOpcode<0xB2, "STHT">;
def STHTI : MMIXStoreImmOpcode<0xB3, "STHT">;

multiclass MMIXLoadPatterns<PatFrag LoadOp, Instruction RegOp,
                            Instruction ImmOp> {
  def : Pat<(i64 (LoadOp GPR64:$base)), (ImmOp GPR64:$base, 0)>;
  def : Pat<(i64 (LoadOp (i64 frameindex:$base))),
            (ImmOp (i64 (to_tframeindex $base)), 0)>;
  def : Pat<(i64 (LoadOp (add GPR64:$base, mmix_uimm8:$offset))),
            (ImmOp GPR64:$base, mmix_uimm8:$offset)>;
  def : Pat<(i64 (LoadOp (add (i64 frameindex:$base),
                              mmix_uimm8:$offset))),
            (ImmOp (i64 (to_tframeindex $base)), mmix_uimm8:$offset)>;
  def : Pat<(i64 (LoadOp (add GPR64:$base, GPR64:$offset))),
            (RegOp GPR64:$base, GPR64:$offset)>;
}

defm : MMIXLoadPatterns<sextloadi8, LDB, LDBI>;
defm : MMIXLoadPatterns<extloadi8, LDBU, LDBUI>;
defm : MMIXLoadPatterns<zextloadi8, LDBU, LDBUI>;
defm : MMIXLoadPatterns<sextloadi16, LDW, LDWI>;
defm : MMIXLoadPatterns<extloadi16, LDWU, LDWUI>;
defm : MMIXLoadPatterns<zextloadi16, LDWU, LDWUI>;
defm : MMIXLoadPatterns<sextloadi32, LDT, LDTI>;
defm : MMIXLoadPatterns<extloadi32, LDTU, LDTUI>;
defm : MMIXLoadPatterns<zextloadi32, LDTU, LDTUI>;
defm : MMIXLoadPatterns<load, LDOU, LDOUI>;

// Naturally aligned atomic reads must not require write permission. Ordering
// is supplied by AtomicExpand's fences; preserve the atomic memory operand.
defm : MMIXLoadPatterns<atomic_load_aext_8, LDBU, LDBUI>;
defm : MMIXLoadPatterns<atomic_load_zext_8, LDBU, LDBUI>;
defm : MMIXLoadPatterns<atomic_load_sext_8, LDB, LDBI>;
defm : MMIXLoadPatterns<atomic_load_aext_16, LDWU, LDWUI>;
defm : MMIXLoadPatterns<atomic_load_zext_16, LDWU, LDWUI>;
defm : MMIXLoadPatterns<atomic_load_sext_16, LDW, LDWI>;
defm : MMIXLoadPatterns<atomic_load_aext_32, LDTU, LDTUI>;
defm : MMIXLoadPatterns<atomic_load_zext_32, LDTU, LDTUI>;
defm : MMIXLoadPatterns<atomic_load_sext_32, LDT, LDTI>;
defm : MMIXLoadPatterns<atomic_load_nonext_64, LDOU, LDOUI>;

multiclass MMIXStorePatterns<PatFrag StoreOp, Instruction RegOp,
                             Instruction ImmOp> {
  def : Pat<(StoreOp (i64 GPR64:$value), GPR64:$base),
            (ImmOp GPR64:$value, GPR64:$base, 0)>;
  def : Pat<(StoreOp (i64 GPR64:$value), (i64 frameindex:$base)),
            (ImmOp GPR64:$value, (i64 (to_tframeindex $base)), 0)>;
  def : Pat<(StoreOp (i64 GPR64:$value),
                     (add GPR64:$base, mmix_uimm8:$offset)),
            (ImmOp GPR64:$value, GPR64:$base, mmix_uimm8:$offset)>;
  def : Pat<(StoreOp (i64 GPR64:$value),
                     (add (i64 frameindex:$base), mmix_uimm8:$offset)),
            (ImmOp GPR64:$value, (i64 (to_tframeindex $base)),
                   mmix_uimm8:$offset)>;
  def : Pat<(StoreOp (i64 GPR64:$value), (add GPR64:$base, GPR64:$offset)),
            (RegOp GPR64:$value, GPR64:$base, GPR64:$offset)>;
}

// Unsigned stores truncate without MMIX's signed-store range exception.
defm : MMIXStorePatterns<truncstorei8, STBU, STBUI>;
defm : MMIXStorePatterns<truncstorei16, STWU, STWUI>;
defm : MMIXStorePatterns<truncstorei32, STTU, STTUI>;
defm : MMIXStorePatterns<store, STOU, STOUI>;

multiclass MMIXF64LoadPatterns<Instruction RegOp, Instruction ImmOp> {
  def : Pat<(f64 (load GPR64:$base)),
            (COPY_TO_REGCLASS (ImmOp GPR64:$base, 0), FPR64)>;
  def : Pat<(f64 (load (i64 frameindex:$base))),
            (COPY_TO_REGCLASS
                (ImmOp (i64 (to_tframeindex $base)), 0), FPR64)>;
  def : Pat<(f64 (load (add GPR64:$base, mmix_uimm8:$offset))),
            (COPY_TO_REGCLASS
                (ImmOp GPR64:$base, mmix_uimm8:$offset), FPR64)>;
  def : Pat<(f64 (load (add (i64 frameindex:$base),
                            mmix_uimm8:$offset))),
            (COPY_TO_REGCLASS
                (ImmOp (i64 (to_tframeindex $base)), mmix_uimm8:$offset),
                FPR64)>;
  def : Pat<(f64 (load (add GPR64:$base, GPR64:$offset))),
            (COPY_TO_REGCLASS
                (RegOp GPR64:$base, GPR64:$offset), FPR64)>;
}

multiclass MMIXF64StorePatterns<Instruction RegOp, Instruction ImmOp> {
  def : Pat<(store (f64 FPR64:$value), GPR64:$base),
            (ImmOp (COPY_TO_REGCLASS FPR64:$value, GPR64),
                   GPR64:$base, 0)>;
  def : Pat<(store (f64 FPR64:$value), (i64 frameindex:$base)),
            (ImmOp (COPY_TO_REGCLASS FPR64:$value, GPR64),
                   (i64 (to_tframeindex $base)), 0)>;
  def : Pat<(store (f64 FPR64:$value),
                   (add GPR64:$base, mmix_uimm8:$offset)),
            (ImmOp (COPY_TO_REGCLASS FPR64:$value, GPR64),
                   GPR64:$base, mmix_uimm8:$offset)>;
  def : Pat<(store (f64 FPR64:$value),
                   (add (i64 frameindex:$base), mmix_uimm8:$offset)),
            (ImmOp (COPY_TO_REGCLASS FPR64:$value, GPR64),
                   (i64 (to_tframeindex $base)), mmix_uimm8:$offset)>;
  def : Pat<(store (f64 FPR64:$value),
                   (add GPR64:$base, GPR64:$offset)),
            (RegOp (COPY_TO_REGCLASS FPR64:$value, GPR64),
                   GPR64:$base, GPR64:$offset)>;
}

defm : MMIXF64LoadPatterns<LDOU, LDOUI>;
defm : MMIXF64StorePatterns<STOU, STOUI>;

// CSWAP compares memory with rP, replaces memory with X on success, writes a
// Boolean result to X, and updates rP with memory on failure. Model X as a
// two-address operand and expose the special-register state explicitly.
class MMIXCompareSwapReg
    : MMIXInstruction<(outs GPR64Op:$result),
                      (ins GPR64Op:$value, GPR64Op:$base, GPR64Op:$offset),
                      "{CSWAP|CSWAP} $result, $base, $offset"> {
  bits<8> result;
  bits<8> base;
  bits<8> offset;
  let MMIXX = result;
  let MMIXY = base;
  let MMIXZ = offset;
  let Constraints = "$result = $value";
  let Uses = [RP];
  let Defs = [RP];
  let mayLoad = true;
  let mayStore = true;
  let MMIXInventoryFamily = "memory";
  let MMIXInventoryForm = "reg-reg-reg";
  let MMIXCanonicalMnemonic = "CSWAP";
  let MMIXALMnemonic = "CSWAP";
  let MMIXALSelection = "register";
  let MMIXALOperandForm = "reg-reg-reg";
}

class MMIXCompareSwapImm
    : MMIXInstruction<(outs GPR64Op:$result),
                      (ins GPR64Op:$value, GPR64Op:$base,
                           MMIXMemOffset8:$offset),
                           "{CSWAP|CSWAP} $result, $base, $offset"> {
  bits<8> result;
  bits<8> base;
  bits<8> offset;
  let MMIXX = result;
  let MMIXY = base;
  let MMIXZ = offset;
  let Constraints = "$result = $value";
  let Uses = [RP];
  let Defs = [RP];
  let mayLoad = true;
  let mayStore = true;
  let MMIXInventoryFamily = "memory";
  let MMIXInventoryForm = "reg-reg-imm8";
  let MMIXCanonicalMnemonic = "CSWAP";
  let MMIXALMnemonic = "CSWAP";
  let MMIXALSelection = "immediate";
  let MMIXALOperandForm = "reg-reg-imm8";
}

class MMIXCompareSwapRegOpcode<bits<8> Opcode>
    : MMIXCompareSwapReg {
  let MMIXOpcode = Opcode;
}

class MMIXCompareSwapImmOpcode<bits<8> Opcode>
    : MMIXCompareSwapImm {
  let MMIXOpcode = Opcode;
}

def CSWAP : MMIXCompareSwapRegOpcode<0x94>;
def CSWAPI : MMIXCompareSwapImmOpcode<0x95>;

class MMIXCondBranch<string Mnemonic>
    : MMIXInstruction<(outs),
                      (ins GPR64Op:$condition, MMIXBranchTarget:$target),
                      Mnemonic # " $condition, $target"> {
  bits<8> condition;
  bits<16> target;
  let MMIXX = condition;
  let MMIXY = target{15-8};
  let MMIXZ = target{7-0};
  let MMIXPCRelativeWidth = 16;
  let MMIXInventoryFamily = "control-flow";
}

class MMIXCondBranchOpcode<bits<8> Opcode, string Mnemonic,
                           string MMIXALName = Mnemonic,
                           string MMIXALDirection = "unclassified",
                           string MMIXALForm = "unclassified">
    : MMIXCondBranch<Mnemonic> {
  let AsmString = "{" # Mnemonic # "|" # MMIXALName #
                      "} $condition, $target";
  let MMIXOpcode = Opcode;
  let MMIXPCRelativeBackward = Opcode{0};
  let MMIXInventoryForm = !if(Opcode{0}, "reg-pcrel16-backward",
                                         "reg-pcrel16-forward");
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = MMIXALName;
  let MMIXALSelection = MMIXALDirection;
  let MMIXALOperandForm = MMIXALForm;
}

def BN   : MMIXCondBranchOpcode<0x40, "BN", "BN", "forward",
                                "reg-pcrel16-forward">;
def BNB  : MMIXCondBranchOpcode<0x41, "BNB", "BN", "backward",
                                "reg-pcrel16-backward">;
def BZ   : MMIXCondBranchOpcode<0x42, "BZ", "BZ", "forward",
                                "reg-pcrel16-forward">;
def BZB  : MMIXCondBranchOpcode<0x43, "BZB", "BZ", "backward",
                                "reg-pcrel16-backward">;
def BP   : MMIXCondBranchOpcode<0x44, "BP", "BP", "forward",
                                "reg-pcrel16-forward">;
def BPB  : MMIXCondBranchOpcode<0x45, "BPB", "BP", "backward",
                                "reg-pcrel16-backward">;
def BOD  : MMIXCondBranchOpcode<0x46, "BOD", "BOD", "forward",
                                "reg-pcrel16-forward">;
def BODB : MMIXCondBranchOpcode<0x47, "BODB", "BOD", "backward",
                                "reg-pcrel16-backward">;
def BNN  : MMIXCondBranchOpcode<0x48, "BNN", "BNN", "forward",
                                "reg-pcrel16-forward">;
def BNNB : MMIXCondBranchOpcode<0x49, "BNNB", "BNN", "backward",
                                "reg-pcrel16-backward">;
def BNZ  : MMIXCondBranchOpcode<0x4A, "BNZ", "BNZ", "forward",
                                "reg-pcrel16-forward">;
def BNZB : MMIXCondBranchOpcode<0x4B, "BNZB", "BNZ", "backward",
                                "reg-pcrel16-backward">;
def BNP  : MMIXCondBranchOpcode<0x4C, "BNP", "BNP", "forward",
                                "reg-pcrel16-forward">;
def BNPB : MMIXCondBranchOpcode<0x4D, "BNPB", "BNP", "backward",
                                "reg-pcrel16-backward">;
def BEV  : MMIXCondBranchOpcode<0x4E, "BEV", "BEV", "forward",
                                "reg-pcrel16-forward">;
def BEVB : MMIXCondBranchOpcode<0x4F, "BEVB", "BEV", "backward",
                                "reg-pcrel16-backward">;

def PBN   : MMIXCondBranchOpcode<0x50, "PBN", "PBN", "forward",
                                 "reg-pcrel16-forward">;
def PBNB  : MMIXCondBranchOpcode<0x51, "PBNB", "PBN", "backward",
                                 "reg-pcrel16-backward">;
def PBZ   : MMIXCondBranchOpcode<0x52, "PBZ", "PBZ", "forward",
                                 "reg-pcrel16-forward">;
def PBZB  : MMIXCondBranchOpcode<0x53, "PBZB", "PBZ", "backward",
                                 "reg-pcrel16-backward">;
def PBP   : MMIXCondBranchOpcode<0x54, "PBP", "PBP", "forward",
                                 "reg-pcrel16-forward">;
def PBPB  : MMIXCondBranchOpcode<0x55, "PBPB", "PBP", "backward",
                                 "reg-pcrel16-backward">;
def PBOD  : MMIXCondBranchOpcode<0x56, "PBOD", "PBOD", "forward",
                                 "reg-pcrel16-forward">;
def PBODB : MMIXCondBranchOpcode<0x57, "PBODB", "PBOD", "backward",
                                 "reg-pcrel16-backward">;
def PBNN  : MMIXCondBranchOpcode<0x58, "PBNN", "PBNN", "forward",
                                 "reg-pcrel16-forward">;
def PBNNB : MMIXCondBranchOpcode<0x59, "PBNNB", "PBNN", "backward",
                                 "reg-pcrel16-backward">;
def PBNZ  : MMIXCondBranchOpcode<0x5A, "PBNZ", "PBNZ", "forward",
                                 "reg-pcrel16-forward">;
def PBNZB : MMIXCondBranchOpcode<0x5B, "PBNZB", "PBNZ", "backward",
                                 "reg-pcrel16-backward">;
def PBNP  : MMIXCondBranchOpcode<0x5C, "PBNP", "PBNP", "forward",
                                 "reg-pcrel16-forward">;
def PBNPB : MMIXCondBranchOpcode<0x5D, "PBNPB", "PBNP", "backward",
                                 "reg-pcrel16-backward">;
def PBEV  : MMIXCondBranchOpcode<0x5E, "PBEV", "PBEV", "forward",
                                 "reg-pcrel16-forward">;
def PBEVB : MMIXCondBranchOpcode<0x5F, "PBEVB", "PBEV", "backward",
                                 "reg-pcrel16-backward">;

class MMIXJump<string Mnemonic>
    : MMIXInstruction<(outs), (ins MMIXJumpTarget:$target),
                      Mnemonic # " $target"> {
  bits<24> target;
  let MMIXX = target{23-16};
  let MMIXY = target{15-8};
  let MMIXZ = target{7-0};
  let MMIXPCRelativeWidth = 24;
  let MMIXInventoryFamily = "control-flow";
}

class MMIXJumpOpcode<bits<8> Opcode, string Mnemonic,
                     string MMIXALName = Mnemonic,
                     string MMIXALDirection = "unclassified",
                     string MMIXALForm = "unclassified">
    : MMIXJump<Mnemonic> {
  let AsmString = "{" # Mnemonic # "|" # MMIXALName # "} $target";
  let MMIXOpcode = Opcode;
  let MMIXPCRelativeBackward = Opcode{0};
  let MMIXInventoryForm = !if(Opcode{0}, "pcrel24-backward",
                                         "pcrel24-forward");
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = MMIXALName;
  let MMIXALSelection = MMIXALDirection;
  let MMIXALOperandForm = MMIXALForm;
}

def JMP  : MMIXJumpOpcode<0xF0, "JMP", "JMP", "forward",
                          "pcrel24-forward">;
def JMPB : MMIXJumpOpcode<0xF1, "JMPB", "JMP", "backward",
                          "pcrel24-backward">;

class MMIXGetAddress<string Mnemonic>
    : MMIXInstruction<(outs GPR64Op:$dst), (ins MMIXBranchTarget:$target),
                      Mnemonic # " $dst, $target"> {
  bits<8> dst;
  bits<16> target;
  let MMIXX = dst;
  let MMIXY = target{15-8};
  let MMIXZ = target{7-0};
  let MMIXPCRelativeWidth = 16;
  let MMIXInventoryFamily = "control-flow";
}

class MMIXGetAddressOpcode<bits<8> Opcode, string Mnemonic,
                           string MMIXALName = Mnemonic,
                           string MMIXALDirection = "unclassified",
                           string MMIXALForm = "unclassified">
    : MMIXGetAddress<Mnemonic> {
  let AsmString = "{" # Mnemonic # "|" # MMIXALName # "} $dst, $target";
  let MMIXOpcode = Opcode;
  let MMIXPCRelativeBackward = Opcode{0};
  let MMIXInventoryForm = !if(Opcode{0}, "reg-pcrel16-backward",
                                         "reg-pcrel16-forward");
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = MMIXALName;
  let MMIXALSelection = MMIXALDirection;
  let MMIXALOperandForm = MMIXALForm;
}

def GETA  : MMIXGetAddressOpcode<0xF4, "GETA", "GETA", "forward",
                                 "reg-pcrel16-forward">;
def GETAB : MMIXGetAddressOpcode<0xF5, "GETAB", "GETA", "backward",
                                 "reg-pcrel16-backward">;

class MMIXPushJump<string Mnemonic>
    : MMIXInstruction<(outs), (ins GPR64Op:$link, MMIXBranchTarget:$target),
                      Mnemonic # " $link, $target"> {
  bits<8> link;
  bits<16> target;
  let MMIXX = link;
  let MMIXY = target{15-8};
  let MMIXZ = target{7-0};
  let MMIXPCRelativeWidth = 16;
  let MMIXInventoryFamily = "control-flow";
}

class MMIXPushJumpOpcode<bits<8> Opcode, string Mnemonic,
                         string MMIXALName = Mnemonic,
                         string MMIXALDirection = "unclassified",
                         string MMIXALForm = "unclassified">
    : MMIXPushJump<Mnemonic> {
  let AsmString = "{" # Mnemonic # "|" # MMIXALName # "} $link, $target";
  let MMIXOpcode = Opcode;
  let MMIXPCRelativeBackward = Opcode{0};
  let MMIXInventoryForm = !if(Opcode{0}, "reg-pcrel16-backward",
                                         "reg-pcrel16-forward");
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = MMIXALName;
  let MMIXALSelection = MMIXALDirection;
  let MMIXALOperandForm = MMIXALForm;
}

def PUSHJ  : MMIXPushJumpOpcode<0xF2, "PUSHJ", "PUSHJ", "forward",
                                 "reg-pcrel16-forward">;
def PUSHJB : MMIXPushJumpOpcode<0xF3, "PUSHJB", "PUSHJ", "backward",
                                 "reg-pcrel16-backward">;

class MMIXGoReg
    : MMIXInstruction<(outs GPR64Op:$link),
                      (ins GPR64Op:$base, GPR64Op:$offset),
                      "{GO|GO} $link, $base, $offset"> {
  bits<8> link;
  bits<8> base;
  bits<8> offset;
  let MMIXX = link;
  let MMIXY = base;
  let MMIXZ = offset;
  let MMIXInventoryFamily = "control-flow";
  let MMIXInventoryForm = "reg-reg-reg";
  let MMIXCanonicalMnemonic = "GO";
  let MMIXALMnemonic = "GO";
  let MMIXALSelection = "register";
  let MMIXALOperandForm = "reg-reg-reg";
}

class MMIXGoImm
    : MMIXInstruction<(outs GPR64Op:$link),
                      (ins GPR64Op:$base, MMIXUImm8:$offset),
                      "{GO|GO} $link, $base, $offset"> {
  bits<8> link;
  bits<8> base;
  bits<8> offset;
  let MMIXX = link;
  let MMIXY = base;
  let MMIXZ = offset;
  let MMIXInventoryFamily = "control-flow";
  let MMIXInventoryForm = "reg-reg-imm8";
  let MMIXCanonicalMnemonic = "GO";
  let MMIXALMnemonic = "GO";
  let MMIXALSelection = "immediate";
  let MMIXALOperandForm = "reg-reg-imm8";
}

class MMIXGoRegOpcode<bits<8> Opcode> : MMIXGoReg {
  let MMIXOpcode = Opcode;
}

class MMIXGoImmOpcode<bits<8> Opcode> : MMIXGoImm {
  let MMIXOpcode = Opcode;
}

def GO  : MMIXGoRegOpcode<0x9E>;
def GOI : MMIXGoImmOpcode<0x9F>;

class MMIXPushGoReg
    : MMIXInstruction<(outs),
                      (ins GPR64Op:$count, GPR64Op:$base, GPR64Op:$offset),
                      "{PUSHGO|PUSHGO} $count, $base, $offset"> {
  bits<8> count;
  bits<8> base;
  bits<8> offset;
  let MMIXX = count;
  let MMIXY = base;
  let MMIXZ = offset;
  let MMIXInventoryFamily = "control-flow";
  let MMIXInventoryForm = "reg-reg-reg";
  let MMIXCanonicalMnemonic = "PUSHGO";
  let MMIXALMnemonic = "PUSHGO";
  let MMIXALSelection = "register";
  let MMIXALOperandForm = "reg-reg-reg";
}

class MMIXPushGoImm
    : MMIXInstruction<(outs),
                      (ins GPR64Op:$count, GPR64Op:$base, MMIXUImm8:$offset),
                      "{PUSHGO|PUSHGO} $count, $base, $offset"> {
  bits<8> count;
  bits<8> base;
  bits<8> offset;
  let MMIXX = count;
  let MMIXY = base;
  let MMIXZ = offset;
  let MMIXInventoryFamily = "control-flow";
  let MMIXInventoryForm = "reg-reg-imm8";
  let MMIXCanonicalMnemonic = "PUSHGO";
  let MMIXALMnemonic = "PUSHGO";
  let MMIXALSelection = "immediate";
  let MMIXALOperandForm = "reg-reg-imm8";
}

class MMIXPushGoRegOpcode<bits<8> Opcode> : MMIXPushGoReg {
  let MMIXOpcode = Opcode;
}

class MMIXPushGoImmOpcode<bits<8> Opcode> : MMIXPushGoImm {
  let MMIXOpcode = Opcode;
}

def PUSHGO  : MMIXPushGoRegOpcode<0xBE>;
def PUSHGOI : MMIXPushGoImmOpcode<0xBF>;

class MMIXPop : MMIXInstruction<(outs),
                                (ins MMIXUImm8:$count, MMIXUImm16:$target),
                                "POP $count, $target"> {
  bits<8> count;
  bits<16> target;
  let MMIXX = count;
  let MMIXY = target{15-8};
  let MMIXZ = target{7-0};
  let MMIXInventoryFamily = "control-flow";
  let MMIXInventoryForm = "imm8-imm16";
  let MMIXCanonicalMnemonic = "POP";
  let MMIXALMnemonic = "POP";
  let MMIXALSelection = "exact";
  let MMIXALOperandForm = "imm8-imm16";
}

def POP : MMIXPop {
  let MMIXOpcode = 0xF8;
  let isReturn = true;
  let isTerminator = true;
  let isBarrier = true;
}

// The MMIX C ABI always returns global results with POP 0,0. Keep these
// fixed CodeGen forms separate from the general architectural POP instruction.
class MMIXReturn<list<Register> ResultUses>
    : MMIXInstruction<(outs), (ins), "POP 0, 0"> {
  let MMIXOpcode = 0xF8;
  let MMIXX = 0;
  let MMIXY = 0;
  let MMIXZ = 0;
  let isCodeGenOnly = true;
  let isReturn = true;
  let isTerminator = true;
  let isBarrier = true;
  let Uses = !listconcat([RJ, RL, RO, RG], ResultUses);
}

def RET : MMIXReturn<[]>;
def RET_VALUE : MMIXReturn<[R231]>;
def RET_PAIR : MMIXReturn<[R231, R232]>;

// Floating-point arithmetic operates on the 64-bit floating-point view of
// the general-purpose register file. These operations have no immediate
// variants in the MMIX instruction set.
class MMIXFPBinary<string Mnemonic>
    : MMIXInstruction<(outs FPR64Op:$dst),
                      (ins FPR64Op:$lhs, FPR64Op:$rhs),
                      Mnemonic # " $dst, $lhs, $rhs"> {
  bits<8> dst;
  bits<8> lhs;
  bits<8> rhs;
  let MMIXX = dst;
  let MMIXY = lhs;
  let MMIXZ = rhs;
  let MMIXInventoryFamily = "floating-point";
  let MMIXInventoryForm = "reg-reg-reg";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "exact";
  let MMIXALOperandForm = "reg-reg-reg";
  let mayRaiseFPException = true;
}

class MMIXFPBinaryOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXFPBinary<Mnemonic> {
  let MMIXOpcode = Opcode;
}

def FADD : MMIXFPBinaryOpcode<0x04, "FADD">;
def FSUB : MMIXFPBinaryOpcode<0x06, "FSUB">;
def FMUL : MMIXFPBinaryOpcode<0x10, "FMUL">;
def FDIV : MMIXFPBinaryOpcode<0x14, "FDIV">;
def FREM : MMIXFPBinaryOpcode<0x16, "FREM">;

def : Pat<(fadd f64:$lhs, f64:$rhs), (FADD $lhs, $rhs)>;
def : Pat<(fsub f64:$lhs, f64:$rhs), (FSUB $lhs, $rhs)>;
def : Pat<(fmul f64:$lhs, f64:$rhs), (FMUL $lhs, $rhs)>;
def : Pat<(fdiv f64:$lhs, f64:$rhs), (FDIV $lhs, $rhs)>;

class MMIXFPCompare<string Mnemonic>
    : MMIXInstruction<(outs GPR64Op:$dst),
                      (ins FPR64Op:$lhs, FPR64Op:$rhs),
                      Mnemonic # " $dst, $lhs, $rhs"> {
  bits<8> dst;
  bits<8> lhs;
  bits<8> rhs;
  let MMIXX = dst;
  let MMIXY = lhs;
  let MMIXZ = rhs;
  let MMIXInventoryFamily = "floating-point";
  let MMIXInventoryForm = "reg-reg-reg";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "exact";
  let MMIXALOperandForm = "reg-reg-reg";
}

class MMIXFPCompareOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXFPCompare<Mnemonic> {
  let MMIXOpcode = Opcode;
}

let mayRaiseFPException = true in {
def FCMP  : MMIXFPCompareOpcode<0x01, "FCMP">;
def FCMPE : MMIXFPCompareOpcode<0x11, "FCMPE">;
def FEQLE : MMIXFPCompareOpcode<0x13, "FEQLE">;
}
def FUN   : MMIXFPCompareOpcode<0x02, "FUN">;
def FUNE  : MMIXFPCompareOpcode<0x12, "FUNE">;
def FEQL  : MMIXFPCompareOpcode<0x03, "FEQL">;

def : Pat<(MMIXfcmp f64:$lhs, f64:$rhs), (FCMP $lhs, $rhs)>;
def : Pat<(MMIXfeql f64:$lhs, f64:$rhs), (FEQL $lhs, $rhs)>;
def : Pat<(MMIXfun f64:$lhs, f64:$rhs), (FUN $lhs, $rhs)>;

class MMIXFPUnaryRounded<string Mnemonic>
    : MMIXInstruction<(outs FPR64Op:$dst),
                      (ins MMIXRoundingMode:$round, FPR64Op:$src),
                      Mnemonic # " $dst, $round, $src"> {
  bits<8> dst;
  bits<3> round;
  bits<8> src;
  let MMIXX = dst;
  let MMIXY{7-3} = 0;
  let MMIXY{2-0} = round;
  let MMIXZ = src;
  let MMIXInventoryFamily = "floating-point";
  let MMIXInventoryForm = "reg-round-reg";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "exact";
  let MMIXALOperandForm = "reg-round-reg";
  let mayRaiseFPException = true;
}

class MMIXFPUnaryRoundedOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXFPUnaryRounded<Mnemonic> {
  let MMIXOpcode = Opcode;
}

def FSQRT : MMIXFPUnaryRoundedOpcode<0x15, "FSQRT">;
def FINT  : MMIXFPUnaryRoundedOpcode<0x17, "FINT">;

// Unconstrained LLVM floating-point operations assume the default
// round-to-nearest mode, independently of the dynamic mode in rA.
def : Pat<(fsqrt f64:$src), (FSQRT 4, $src)>;
def : Pat<(ftrunc f64:$src), (FINT 1, $src)>;
def : Pat<(fceil f64:$src), (FINT 2, $src)>;
def : Pat<(ffloor f64:$src), (FINT 3, $src)>;
def : Pat<(froundeven f64:$src), (FINT 4, $src)>;
def : Pat<(frint f64:$src), (FINT 4, $src)>;

class MMIXFix<string Mnemonic>
    : MMIXInstruction<(outs GPR64Op:$dst),
                      (ins MMIXRoundingMode:$round, FPR64Op:$src),
                      Mnemonic # " $dst, $round, $src"> {
  bits<8> dst;
  bits<3> round;
  bits<8> src;
  let MMIXX = dst;
  let MMIXY{7-3} = 0;
  let MMIXY{2-0} = round;
  let MMIXZ = src;
  let MMIXInventoryFamily = "floating-point";
  let MMIXInventoryForm = "reg-round-reg";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "exact";
  let MMIXALOperandForm = "reg-round-reg";
  let mayRaiseFPException = true;
}

class MMIXFixOpcode<bits<8> Opcode, string Mnemonic> : MMIXFix<Mnemonic> {
  let MMIXOpcode = Opcode;
}

def FIX  : MMIXFixOpcode<0x05, "FIX">;
def FIXU : MMIXFixOpcode<0x07, "FIXU">;

class MMIXFloatFromIntReg<string Mnemonic>
    : MMIXInstruction<(outs FPR64Op:$dst),
                      (ins MMIXRoundingMode:$round, GPR64Op:$src),
                      "{" # Mnemonic # "|" # Mnemonic #
                          "} $dst, $round, $src"> {
  bits<8> dst;
  bits<3> round;
  bits<8> src;
  let MMIXX = dst;
  let MMIXY{7-3} = 0;
  let MMIXY{2-0} = round;
  let MMIXZ = src;
  let MMIXInventoryFamily = "floating-point";
  let MMIXInventoryForm = "reg-round-reg";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "register";
  let MMIXALOperandForm = "reg-round-reg";
  string MMIXSourceForm = "register";
  let mayRaiseFPException = true;
}

class MMIXFloatFromIntImm<string Mnemonic>
    : MMIXInstruction<(outs FPR64Op:$dst),
                      (ins MMIXRoundingMode:$round, MMIXUImm8:$src),
                      "{" # Mnemonic # "|" # Mnemonic #
                          "} $dst, $round, $src"> {
  bits<8> dst;
  bits<3> round;
  bits<8> src;
  let MMIXX = dst;
  let MMIXY{7-3} = 0;
  let MMIXY{2-0} = round;
  let MMIXZ = src;
  let MMIXInventoryFamily = "floating-point";
  let MMIXInventoryForm = "reg-round-imm8";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "immediate";
  let MMIXALOperandForm = "reg-round-imm8";
  string MMIXSourceForm = "immediate";
  let mayRaiseFPException = true;
}

class MMIXFloatFromIntRegOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXFloatFromIntReg<Mnemonic> {
  let MMIXOpcode = Opcode;
}

class MMIXFloatFromIntImmOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXFloatFromIntImm<Mnemonic> {
  let MMIXOpcode = Opcode;
}

def FLOT   : MMIXFloatFromIntRegOpcode<0x08, "FLOT">;
def FLOTI  : MMIXFloatFromIntImmOpcode<0x09, "FLOT">;
def FLOTU  : MMIXFloatFromIntRegOpcode<0x0A, "FLOTU">;
def FLOTUI : MMIXFloatFromIntImmOpcode<0x0B, "FLOTU">;
def SFLOT  : MMIXFloatFromIntRegOpcode<0x0C, "SFLOT">;
def SFLOTI : MMIXFloatFromIntImmOpcode<0x0D, "SFLOT">;
def SFLOTU : MMIXFloatFromIntRegOpcode<0x0E, "SFLOTU">;
def SFLOTUI : MMIXFloatFromIntImmOpcode<0x0F, "SFLOTU">;

def : Pat<(MMIXflot mmix_uimm8:$src), (FLOTI 4, mmix_uimm8:$src)>;
def : Pat<(MMIXflot i64:$src), (FLOT 4, $src)>;
def : Pat<(MMIXflotu mmix_uimm8:$src), (FLOTUI 4, mmix_uimm8:$src)>;
def : Pat<(MMIXflotu i64:$src), (FLOTU 4, $src)>;
def : Pat<(MMIXsflot mmix_uimm8:$src), (SFLOTI 4, mmix_uimm8:$src)>;
def : Pat<(MMIXsflot i64:$src), (SFLOT 4, $src)>;
def : Pat<(MMIXsflotu mmix_uimm8:$src), (SFLOTUI 4, mmix_uimm8:$src)>;
def : Pat<(MMIXsflotu i64:$src), (SFLOTU 4, $src)>;
def : Pat<(MMIXfixu f64:$src), (FIXU 1, $src)>;

// Unconstrained LLVM integer-to-floating conversions always round to nearest,
// while floating-to-integer conversions truncate toward zero. FIXU avoids the
// float-to-fix trap of FIX for LLVM's poison-producing out-of-range inputs.

// Short floating-point values are converted to and from the 64-bit floating
// representation when crossing memory.
class MMIXFPLoadReg<string Mnemonic>
    : MMIXInstruction<(outs FPR64Op:$dst),
                      (ins GPR64Op:$base, GPR64Op:$offset),
                      "{" # Mnemonic # "|" # Mnemonic #
                          "} $dst, $base, $offset"> {
  bits<8> dst;
  bits<8> base;
  bits<8> offset;
  let MMIXX = dst;
  let MMIXY = base;
  let MMIXZ = offset;
  let mayLoad = true;
  let MMIXInventoryFamily = "floating-point";
  let MMIXInventoryForm = "reg-reg-reg";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "register";
  let MMIXALOperandForm = "reg-reg-reg";
}

class MMIXFPLoadImm<string Mnemonic>
    : MMIXInstruction<(outs FPR64Op:$dst),
                      (ins GPR64Op:$base, MMIXMemOffset8:$offset),
                      "{" # Mnemonic # "|" # Mnemonic #
                          "} $dst, $base, $offset"> {
  bits<8> dst;
  bits<8> base;
  bits<8> offset;
  let MMIXX = dst;
  let MMIXY = base;
  let MMIXZ = offset;
  let mayLoad = true;
  let MMIXInventoryFamily = "floating-point";
  let MMIXInventoryForm = "reg-reg-imm8";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "immediate";
  let MMIXALOperandForm = "reg-reg-imm8";
}

class MMIXFPStoreReg<string Mnemonic>
    : MMIXInstruction<(outs),
                      (ins FPR64Op:$value, GPR64Op:$base, GPR64Op:$offset),
                      "{" # Mnemonic # "|" # Mnemonic #
                          "} $value, $base, $offset"> {
  bits<8> value;
  bits<8> base;
  bits<8> offset;
  let MMIXX = value;
  let MMIXY = base;
  let MMIXZ = offset;
  let mayStore = true;
  let MMIXInventoryFamily = "floating-point";
  let MMIXInventoryForm = "reg-reg-reg";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "register";
  let MMIXALOperandForm = "reg-reg-reg";
  let mayRaiseFPException = true;
}

class MMIXFPStoreImm<string Mnemonic>
    : MMIXInstruction<(outs),
                      (ins FPR64Op:$value, GPR64Op:$base,
                           MMIXMemOffset8:$offset),
                      "{" # Mnemonic # "|" # Mnemonic #
                          "} $value, $base, $offset"> {
  bits<8> value;
  bits<8> base;
  bits<8> offset;
  let MMIXX = value;
  let MMIXY = base;
  let MMIXZ = offset;
  let mayStore = true;
  let MMIXInventoryFamily = "floating-point";
  let MMIXInventoryForm = "reg-reg-imm8";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "immediate";
  let MMIXALOperandForm = "reg-reg-imm8";
  let mayRaiseFPException = true;
}

class MMIXFPLoadRegOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXFPLoadReg<Mnemonic> {
  let MMIXOpcode = Opcode;
}

class MMIXFPLoadImmOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXFPLoadImm<Mnemonic> {
  let MMIXOpcode = Opcode;
}

class MMIXFPStoreRegOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXFPStoreReg<Mnemonic> {
  let MMIXOpcode = Opcode;
}

class MMIXFPStoreImmOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXFPStoreImm<Mnemonic> {
  let MMIXOpcode = Opcode;
}

def LDSF : MMIXFPLoadRegOpcode<0x90, "LDSF">;
def LDSFI : MMIXFPLoadImmOpcode<0x91, "LDSF">;
def STSF : MMIXFPStoreRegOpcode<0xB0, "STSF">;
def STSFI : MMIXFPStoreImmOpcode<0xB1, "STSF">;

multiclass MMIXShortFloatLoadPatterns<Instruction RegOp, Instruction ImmOp> {
  def : Pat<(f64 (extloadf32 GPR64:$base)), (ImmOp GPR64:$base, 0)>;
  def : Pat<(f64 (extloadf32 (i64 frameindex:$base))),
            (ImmOp (i64 (to_tframeindex $base)), 0)>;
  def : Pat<(f64 (extloadf32 (add GPR64:$base, mmix_uimm8:$offset))),
            (ImmOp GPR64:$base, mmix_uimm8:$offset)>;
  def : Pat<(f64 (extloadf32 (add (i64 frameindex:$base),
                                  mmix_uimm8:$offset))),
            (ImmOp (i64 (to_tframeindex $base)), mmix_uimm8:$offset)>;
  def : Pat<(f64 (extloadf32 (add GPR64:$base, GPR64:$offset))),
            (RegOp GPR64:$base, GPR64:$offset)>;
}

multiclass MMIXShortFloatStorePatterns<Instruction RegOp, Instruction ImmOp> {
  def : Pat<(truncstoref32 (f64 FPR64:$value), GPR64:$base),
            (ImmOp FPR64:$value, GPR64:$base, 0)>;
  def : Pat<(truncstoref32 (f64 FPR64:$value), (i64 frameindex:$base)),
            (ImmOp FPR64:$value, (i64 (to_tframeindex $base)), 0)>;
  def : Pat<(truncstoref32 (f64 FPR64:$value),
                          (add GPR64:$base, mmix_uimm8:$offset)),
            (ImmOp FPR64:$value, GPR64:$base, mmix_uimm8:$offset)>;
  def : Pat<(truncstoref32 (f64 FPR64:$value),
                          (add (i64 frameindex:$base),
                               mmix_uimm8:$offset)),
            (ImmOp FPR64:$value, (i64 (to_tframeindex $base)),
                   mmix_uimm8:$offset)>;
  def : Pat<(truncstoref32 (f64 FPR64:$value),
                          (add GPR64:$base, GPR64:$offset)),
            (RegOp FPR64:$value, GPR64:$base, GPR64:$offset)>;
}

defm : MMIXShortFloatLoadPatterns<LDSF, LDSFI>;
defm : MMIXShortFloatStorePatterns<STSF, STSFI>;

// Machine-control instructions are intentionally modeled as side-effecting
// operations. They are available to MC and target-specific mechanisms, but
// are not generic instruction-selection candidates in this phase.
class MMIXMachineInstruction<dag outs, dag ins, string asmstr>
    : MMIXInstruction<outs, ins, asmstr> {
  let hasSideEffects = true;
  let MMIXInventoryFamily = "system";
}

class MMIXSystemInstruction<dag outs, dag ins, string asmstr>
    : MMIXMachineInstruction<outs, ins, asmstr> {
  let Predicates = [HasMMIXSystem];
  let MMIXInventoryFeature = "system";
}

class MMIXCacheInstruction<dag outs, dag ins, string asmstr>
    : MMIXMachineInstruction<outs, ins, asmstr> {
  let Predicates = [HasMMIXCache];
  let MMIXInventoryFeature = "cache";
}

class MMIXVirtualMemoryInstruction<dag outs, dag ins, string asmstr>
    : MMIXMachineInstruction<outs, ins, asmstr> {
  let Predicates = [HasMMIXVirtualMemory];
  let MMIXInventoryFeature = "virtual-memory";
}

class MMIXStoreConstantReg
    : MMIXMachineInstruction<(outs),
                             (ins MMIXUImm8:$value, GPR64Op:$base,
                                  GPR64Op:$offset),
                             "{STCO|STCO} $value, $base, $offset"> {
  bits<8> value;
  bits<8> base;
  bits<8> offset;
  let MMIXX = value;
  let MMIXY = base;
  let MMIXZ = offset;
  let mayStore = true;
  let MMIXInventoryForm = "imm8-reg-reg";
  let MMIXCanonicalMnemonic = "STCO";
  let MMIXALMnemonic = "STCO";
  let MMIXALSelection = "register";
  let MMIXALOperandForm = "imm8-reg-reg";
}

class MMIXStoreConstantImm
    : MMIXMachineInstruction<(outs),
                             (ins MMIXUImm8:$value, GPR64Op:$base,
                                  MMIXMemOffset8:$offset),
                             "{STCO|STCO} $value, $base, $offset"> {
  bits<8> value;
  bits<8> base;
  bits<8> offset;
  let MMIXX = value;
  let MMIXY = base;
  let MMIXZ = offset;
  let mayStore = true;
  let MMIXInventoryForm = "imm8-reg-imm8";
  let MMIXCanonicalMnemonic = "STCO";
  let MMIXALMnemonic = "STCO";
  let MMIXALSelection = "immediate";
  let MMIXALOperandForm = "imm8-reg-imm8";
}

class MMIXStoreConstantRegOpcode<bits<8> Opcode>
    : MMIXStoreConstantReg {
  let MMIXOpcode = Opcode;
}

class MMIXStoreConstantImmOpcode<bits<8> Opcode>
    : MMIXStoreConstantImm {
  let MMIXOpcode = Opcode;
}

def STCO : MMIXStoreConstantRegOpcode<0xB4>;
def STCOI : MMIXStoreConstantImmOpcode<0xB5>;

class MMIXCacheReg<string Mnemonic>
    : MMIXCacheInstruction<(outs),
                           (ins MMIXUImm8:$count, GPR64Op:$base,
                                GPR64Op:$offset),
                           "{" # Mnemonic # "|" # Mnemonic #
                               "} $count, $base, $offset"> {
  bits<8> count;
  bits<8> base;
  bits<8> offset;
  let MMIXX = count;
  let MMIXY = base;
  let MMIXZ = offset;
  let MMIXInventoryForm = "imm8-reg-reg";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "register";
  let MMIXALOperandForm = "imm8-reg-reg";
}

class MMIXCacheImm<string Mnemonic>
    : MMIXCacheInstruction<(outs),
                           (ins MMIXUImm8:$count, GPR64Op:$base,
                                MMIXMemOffset8:$offset),
                           "{" # Mnemonic # "|" # Mnemonic #
                               "} $count, $base, $offset"> {
  bits<8> count;
  bits<8> base;
  bits<8> offset;
  let MMIXX = count;
  let MMIXY = base;
  let MMIXZ = offset;
  let MMIXInventoryForm = "imm8-reg-imm8";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "immediate";
  let MMIXALOperandForm = "imm8-reg-imm8";
}

class MMIXCacheRegOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXCacheReg<Mnemonic> {
  let MMIXOpcode = Opcode;
}

class MMIXCacheImmOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXCacheImm<Mnemonic> {
  let MMIXOpcode = Opcode;
}

def PRELD   : MMIXCacheRegOpcode<0x9A, "PRELD">;
def PRELDI  : MMIXCacheImmOpcode<0x9B, "PRELD">;
def PREGO   : MMIXCacheRegOpcode<0x9C, "PREGO">;
def PREGOI  : MMIXCacheImmOpcode<0x9D, "PREGO">;
def PREST   : MMIXCacheRegOpcode<0xBA, "PREST">;
def PRESTI  : MMIXCacheImmOpcode<0xBB, "PREST">;
let mayLoad = true, mayStore = true in {
def SYNCD   : MMIXCacheRegOpcode<0xB8, "SYNCD">;
def SYNCDI  : MMIXCacheImmOpcode<0xB9, "SYNCD">;
def SYNCID  : MMIXCacheRegOpcode<0xBC, "SYNCID">;
def SYNCIDI : MMIXCacheImmOpcode<0xBD, "SYNCID">;
}

class MMIXUncachedLoad<string Mnemonic>
    : MMIXCacheInstruction<(outs GPR64Op:$dst),
                           (ins GPR64Op:$base, GPR64Op:$offset),
                           "{" # Mnemonic # "|" # Mnemonic #
                               "} $dst, $base, $offset"> {
  bits<8> dst;
  bits<8> base;
  bits<8> offset;
  let MMIXX = dst;
  let MMIXY = base;
  let MMIXZ = offset;
  let mayLoad = true;
  let MMIXInventoryForm = "reg-reg-reg";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "register";
  let MMIXALOperandForm = "reg-reg-reg";
}

class MMIXUncachedLoadImm<string Mnemonic>
    : MMIXCacheInstruction<(outs GPR64Op:$dst),
                           (ins GPR64Op:$base, MMIXMemOffset8:$offset),
                           "{" # Mnemonic # "|" # Mnemonic #
                               "} $dst, $base, $offset"> {
  bits<8> dst;
  bits<8> base;
  bits<8> offset;
  let MMIXX = dst;
  let MMIXY = base;
  let MMIXZ = offset;
  let mayLoad = true;
  let MMIXInventoryForm = "reg-reg-imm8";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "immediate";
  let MMIXALOperandForm = "reg-reg-imm8";
}

class MMIXUncachedStore<string Mnemonic>
    : MMIXCacheInstruction<(outs),
                           (ins GPR64Op:$value, GPR64Op:$base,
                                GPR64Op:$offset),
                           "{" # Mnemonic # "|" # Mnemonic #
                               "} $value, $base, $offset"> {
  bits<8> value;
  bits<8> base;
  bits<8> offset;
  let MMIXX = value;
  let MMIXY = base;
  let MMIXZ = offset;
  let mayStore = true;
  let MMIXInventoryForm = "reg-reg-reg";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "register";
  let MMIXALOperandForm = "reg-reg-reg";
}

class MMIXUncachedStoreImm<string Mnemonic>
    : MMIXCacheInstruction<(outs),
                           (ins GPR64Op:$value, GPR64Op:$base,
                                MMIXMemOffset8:$offset),
                           "{" # Mnemonic # "|" # Mnemonic #
                               "} $value, $base, $offset"> {
  bits<8> value;
  bits<8> base;
  bits<8> offset;
  let MMIXX = value;
  let MMIXY = base;
  let MMIXZ = offset;
  let mayStore = true;
  let MMIXInventoryForm = "reg-reg-imm8";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "immediate";
  let MMIXALOperandForm = "reg-reg-imm8";
}

class MMIXUncachedLoadOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXUncachedLoad<Mnemonic> {
  let MMIXOpcode = Opcode;
}

class MMIXUncachedLoadImmOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXUncachedLoadImm<Mnemonic> {
  let MMIXOpcode = Opcode;
}

class MMIXUncachedStoreOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXUncachedStore<Mnemonic> {
  let MMIXOpcode = Opcode;
}

class MMIXUncachedStoreImmOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXUncachedStoreImm<Mnemonic> {
  let MMIXOpcode = Opcode;
}

def LDUNC : MMIXUncachedLoadOpcode<0x96, "LDUNC">;
def LDUNCI : MMIXUncachedLoadImmOpcode<0x97, "LDUNC">;
def STUNC : MMIXUncachedStoreOpcode<0xB6, "STUNC">;
def STUNCI : MMIXUncachedStoreImmOpcode<0xB7, "STUNC">;

class MMIXVirtualTranslationReg
    : MMIXVirtualMemoryInstruction<(outs GPR64Op:$dst),
                                   (ins GPR64Op:$base, GPR64Op:$offset),
                                   "{LDVTS|LDVTS} $dst, $base, $offset"> {
  bits<8> dst;
  bits<8> base;
  bits<8> offset;
  let MMIXX = dst;
  let MMIXY = base;
  let MMIXZ = offset;
  let MMIXInventoryForm = "reg-reg-reg";
  let MMIXCanonicalMnemonic = "LDVTS";
  let MMIXALMnemonic = "LDVTS";
  let MMIXALSelection = "register";
  let MMIXALOperandForm = "reg-reg-reg";
}

class MMIXVirtualTranslationImm
    : MMIXVirtualMemoryInstruction<(outs GPR64Op:$dst),
                                   (ins GPR64Op:$base,
                                        MMIXMemOffset8:$offset),
                                   "{LDVTS|LDVTS} $dst, $base, $offset"> {
  bits<8> dst;
  bits<8> base;
  bits<8> offset;
  let MMIXX = dst;
  let MMIXY = base;
  let MMIXZ = offset;
  let MMIXInventoryForm = "reg-reg-imm8";
  let MMIXCanonicalMnemonic = "LDVTS";
  let MMIXALMnemonic = "LDVTS";
  let MMIXALSelection = "immediate";
  let MMIXALOperandForm = "reg-reg-imm8";
}

class MMIXVirtualTranslationRegOpcode<bits<8> Opcode>
    : MMIXVirtualTranslationReg {
  let MMIXOpcode = Opcode;
}

class MMIXVirtualTranslationImmOpcode<bits<8> Opcode>
    : MMIXVirtualTranslationImm {
  let MMIXOpcode = Opcode;
}

def LDVTS  : MMIXVirtualTranslationRegOpcode<0x98>;
def LDVTSI : MMIXVirtualTranslationImmOpcode<0x99>;

class MMIXSync : MMIXSystemInstruction<(outs), (ins MMIXSyncMode:$mode),
                                       "SYNC $mode"> {
  bits<3> mode;
  let MMIXX = 0;
  let MMIXY = 0;
  let MMIXZ{7-3} = 0;
  let MMIXZ{2-0} = mode;
  let MMIXInventoryForm = "sync-mode";
  let MMIXCanonicalMnemonic = "SYNC";
  let MMIXALMnemonic = "SYNC";
  let MMIXALSelection = "exact";
  let MMIXALOperandForm = "sync-mode";
}

def SYNC : MMIXSync {
  let MMIXOpcode = 0xFC;
}

class MMIXSwym : MMIXMachineInstruction<
    (outs),
    (ins MMIXRegOrImm8:$x, MMIXRegOrImm8:$y, MMIXRegOrImm8:$z),
    "SWYM $x, $y, $z"> {
  bits<8> x;
  bits<8> y;
  bits<8> z;
  let MMIXX = x;
  let MMIXY = y;
  let MMIXZ = z;
  let MMIXInventoryForm = "reg-or-imm8-reg-or-imm8-reg-or-imm8";
  let MMIXCanonicalMnemonic = "SWYM";
  let MMIXALMnemonic = "SWYM";
  let MMIXALSelection = "exact";
  let MMIXALOperandForm = "reg-or-imm8-reg-or-imm8-reg-or-imm8";
}

def SWYM : MMIXSwym {
  let MMIXOpcode = 0xFD;
}

class MMIXTrapInstruction<string Mnemonic>
    : MMIXSystemInstruction<
          (outs),
          (ins MMIXRegOrImm8:$x, MMIXRegOrImm8:$y, MMIXRegOrImm8:$z),
          Mnemonic # " $x, $y, $z"> {
  bits<8> x;
  bits<8> y;
  bits<8> z;
  let MMIXX = x;
  let MMIXY = y;
  let MMIXZ = z;
  let MMIXInventoryForm = "reg-or-imm8-reg-or-imm8-reg-or-imm8";
  let MMIXCanonicalMnemonic = Mnemonic;
  let MMIXALMnemonic = Mnemonic;
  let MMIXALSelection = "exact";
  let MMIXALOperandForm = "reg-or-imm8-reg-or-imm8-reg-or-imm8";
}

class MMIXTrapInstructionOpcode<bits<8> Opcode, string Mnemonic>
    : MMIXTrapInstruction<Mnemonic> {
  let MMIXOpcode = Opcode;
}

def TRAP : MMIXTrapInstructionOpcode<0x00, "TRAP"> {
  let Uses = [R255];
  let Defs = [R255];
}
def TRIP : MMIXTrapInstructionOpcode<0xFF, "TRIP">;

class MMIXResume
    : MMIXSystemInstruction<(outs), (ins MMIXResumeMode:$mode),
                            "RESUME $mode"> {
  bits<1> mode;
  let MMIXX = 0;
  let MMIXY = 0;
  let MMIXZ{7-1} = 0;
  let MMIXZ{0} = mode;
  let MMIXInventoryForm = "resume-mode";
  let MMIXCanonicalMnemonic = "RESUME";
  let MMIXALMnemonic = "RESUME";
  let MMIXALSelection = "exact";
  let MMIXALOperandForm = "resume-mode";
}

def RESUME : MMIXResume {
  let MMIXOpcode = 0xF9;
}

class MMIXGet : MMIXMachineInstruction<(outs GPR64Op:$dst), (ins SPR64Op:$src),
                                        "GET $dst, $src"> {
  bits<8> dst;
  bits<5> src;
  let MMIXX = dst;
  let MMIXY = 0;
  let MMIXZ{7-5} = 0;
  let MMIXZ{4-0} = src;
  let MMIXInventoryForm = "reg-special-reg";
  let MMIXCanonicalMnemonic = "GET";
  let MMIXALMnemonic = "GET";
  let MMIXALSelection = "exact";
  let MMIXALOperandForm = "reg-special-reg";
}

def GET : MMIXGet {
  let MMIXOpcode = 0xFE;
}

class MMIXPutReg
    : MMIXMachineInstruction<(outs SPR64Op:$dst), (ins GPR64Op:$src),
                             "{PUT|PUT} $dst, $src"> {
  bits<5> dst;
  bits<8> src;
  let MMIXX{7-5} = 0;
  let MMIXX{4-0} = dst;
  let MMIXY = 0;
  let MMIXZ = src;
  let MMIXInventoryForm = "special-reg-reg";
  let MMIXCanonicalMnemonic = "PUT";
  let MMIXALMnemonic = "PUT";
  let MMIXALSelection = "register";
  let MMIXALOperandForm = "special-reg-reg";
}

class MMIXPutImm
    : MMIXMachineInstruction<(outs SPR64Op:$dst), (ins MMIXUImm8:$src),
                             "{PUT|PUT} $dst, $src"> {
  bits<5> dst;
  bits<8> src;
  let MMIXX{7-5} = 0;
  let MMIXX{4-0} = dst;
  let MMIXY = 0;
  let MMIXZ = src;
  let MMIXInventoryForm = "special-reg-imm8";
  let MMIXCanonicalMnemonic = "PUT";
  let MMIXALMnemonic = "PUT";
  let MMIXALSelection = "immediate";
  let MMIXALOperandForm = "special-reg-imm8";
}

class MMIXPutRegOpcode<bits<8> Opcode> : MMIXPutReg {
  let MMIXOpcode = Opcode;
}

class MMIXPutImmOpcode<bits<8> Opcode> : MMIXPutImm {
  let MMIXOpcode = Opcode;
}

def PUT  : MMIXPutRegOpcode<0xF6>;
def PUTI : MMIXPutImmOpcode<0xF7>;

class MMIXSave : MMIXSystemInstruction<(outs GPR64Op:$state), (ins),
                                        "{SAVE $state|SAVE $state, 0}"> {
  bits<8> state;
  let MMIXX = state;
  let MMIXY = 0;
  let MMIXZ = 0;
  let MMIXInventoryForm = "reg";
  let MMIXCanonicalMnemonic = "SAVE";
  let MMIXALMnemonic = "SAVE";
  let MMIXALSelection = "exact";
  let MMIXALOperandForm = "reg";
}
// MMIXAL's one-operand UNSAVE form fixes the architectural X and Y fields to
// zero; unlike SAVE, it does not take a source-visible zero operand.
class MMIXUnsave : MMIXSystemInstruction<(outs), (ins GPR64Op:$state),
                                          "UNSAVE $state"> {
  bits<8> state;
  let MMIXX = 0;
  let MMIXY = 0;
  let MMIXZ = state;
  let MMIXInventoryForm = "reg";
  let MMIXCanonicalMnemonic = "UNSAVE";
  let MMIXALMnemonic = "UNSAVE";
  let MMIXALSelection = "exact";
  let MMIXALOperandForm = "reg";
}

def SAVE : MMIXSave {
  let MMIXOpcode = 0xFA;
}
def UNSAVE : MMIXUnsave {
  let MMIXOpcode = 0xFB;
}

// Keep CodeGen-only pseudos separate from the architectural instruction
// inventory and its encoding fields.
class MMIXPseudoInstruction<dag outs, dag ins, string asmstr>
    : Instruction {
  let Namespace = "MMIX";
  let OutOperandList = outs;
  let InOperandList = ins;
  let AsmString = asmstr;
  let Size = 0;
  let isPseudo = true;
  let isCodeGenOnly = true;
}

class MMIXPutSpecialRegisterPseudo<Register Reg, dag ins, bit IsImmediate>
    : MMIXPseudoInstruction<(outs), ins, ""> {
  let Defs = [Reg];
  let hasSideEffects = true;
  let TSFlags{14} = true;
  let TSFlags{15} = IsImmediate;
}

multiclass MMIXPutSpecialRegisterPseudos<Register Reg> {
  def _REG
      : MMIXPutSpecialRegisterPseudo<Reg, (ins GPR64Op:$src), false>;
  def _IMM
      : MMIXPutSpecialRegisterPseudo<Reg, (ins MMIXUImm8:$src), true>;
}

defm PUT_RB  : MMIXPutSpecialRegisterPseudos<RB>;
defm PUT_RD  : MMIXPutSpecialRegisterPseudos<RD>;
defm PUT_RE  : MMIXPutSpecialRegisterPseudos<RE>;
defm PUT_RH  : MMIXPutSpecialRegisterPseudos<RH>;
defm PUT_RJ  : MMIXPutSpecialRegisterPseudos<RJ>;
defm PUT_RM  : MMIXPutSpecialRegisterPseudos<RM>;
defm PUT_RR  : MMIXPutSpecialRegisterPseudos<RR>;
defm PUT_RBB : MMIXPutSpecialRegisterPseudos<RBB>;
defm PUT_RC  : MMIXPutSpecialRegisterPseudos<RC>;
defm PUT_RN  : MMIXPutSpecialRegisterPseudos<RN>;
defm PUT_RO  : MMIXPutSpecialRegisterPseudos<RO>;
defm PUT_RS  : MMIXPutSpecialRegisterPseudos<RS>;
defm PUT_RI  : MMIXPutSpecialRegisterPseudos<RI>;
defm PUT_RT  : MMIXPutSpecialRegisterPseudos<RT>;
defm PUT_RTT : MMIXPutSpecialRegisterPseudos<RTT>;
defm PUT_RK  : MMIXPutSpecialRegisterPseudos<RK>;
defm PUT_RQ  : MMIXPutSpecialRegisterPseudos<RQ>;
defm PUT_RU  : MMIXPutSpecialRegisterPseudos<RU>;
defm PUT_RV  : MMIXPutSpecialRegisterPseudos<RV>;
defm PUT_RG  : MMIXPutSpecialRegisterPseudos<RG>;
defm PUT_RL  : MMIXPutSpecialRegisterPseudos<RL>;
defm PUT_RA  : MMIXPutSpecialRegisterPseudos<RA>;
defm PUT_RF  : MMIXPutSpecialRegisterPseudos<RF>;
defm PUT_RP  : MMIXPutSpecialRegisterPseudos<RP>;
defm PUT_RW  : MMIXPutSpecialRegisterPseudos<RW>;
defm PUT_RX  : MMIXPutSpecialRegisterPseudos<RX>;
defm PUT_RY  : MMIXPutSpecialRegisterPseudos<RY>;
defm PUT_RZ  : MMIXPutSpecialRegisterPseudos<RZ>;
defm PUT_RWW : MMIXPutSpecialRegisterPseudos<RWW>;
defm PUT_RXX : MMIXPutSpecialRegisterPseudos<RXX>;
defm PUT_RYY : MMIXPutSpecialRegisterPseudos<RYY>;
defm PUT_RZZ : MMIXPutSpecialRegisterPseudos<RZZ>;

// AtomicExpand reduces all supported atomic operations to aligned octabyte
// compare-and-swap. This pseudo keeps PUT/CSWAP/GET indivisible to the
// scheduler and is expanded after physical register allocation.
def ATOMIC_CMP_SWAP
    : MMIXPseudoInstruction<(outs GPR64Op:$old),
                            (ins GPR64Op:$ptr, GPR64Op:$expected,
                                 GPR64Op:$new), ""> {
  let mayLoad = true;
  let mayStore = true;
  let hasSideEffects = true;
}

def : Pat<(atomic_cmp_swap_i64 GPR64:$ptr, GPR64:$expected, GPR64:$new),
          (ATOMIC_CMP_SWAP GPR64:$ptr, GPR64:$expected, GPR64:$new)>;

def TRAP_STATE
    : MMIXPseudoInstruction<(outs GPR64Op:$result),
                            (ins GPR64Op:$argument, MMIXUImm8:$service,
                                 MMIXUImm8:$handle), ""> {
  let Defs = [R255];
  let mayLoad = true;
  let mayStore = true;
  let hasSideEffects = true;
}

// Keep generic non-returning traps distinct from the returning, operand-bearing
// llvm.mmix.trap interface even though both encode architectural TRAPs.
def GENERIC_TRAP : MMIXInstruction<(outs), (ins), "TRAP 255, 0, 0"> {
  let MMIXOpcode = 0x00;
  let MMIXX = 255;
  let MMIXY = 0;
  let MMIXZ = 0;
  let isCodeGenOnly = true;
  let Pattern = [(trap)];
  let isTerminator = true;
  let isBarrier = true;
  let hasSideEffects = true;
}

// Mode 3 orders every load or store before the fence with respect to every
// load or store after it, which implements LLVM's system-scope fences.
def : Pat<(atomic_fence timm, timm), (SYNC 3)>;

// Call-site lowering records the complete call state until physical registers
// are assigned. Post-RA expansion then selects the procedure form and supplies
// the register-stack operand.
// R251 is reserved but call-clobbered. An explicit def prevents its incoming
// sret value from being treated as an invariant physical register by sinking.
def MMIXCallTarget : Operand<i64>;
let isCall = true, hasSideEffects = true, Uses = [R254, RG, RL, RO],
    Defs = [RJ, RL, RO, R251] in {
  def CALL_STATE
      : MMIXPseudoInstruction<(outs), (ins MMIXCallTarget:$callee),
                              "# CALL_STATE $callee">;
  def DIRECT_CALL_STATE
      : MMIXPseudoInstruction<
            (outs), (ins MMIXCallTarget:$callee, GPR64Op:$scratch),
            "# DIRECT_CALL_STATE $callee, $scratch">;
}

// A tail transfer is both the final call and the return from the current
// function. The variable operands begin with a register mask and continue
// with the live argument-register uses. Later tasks lower these state pseudos
// to terminal JMP/GO sequences after restoring the software frame and rJ.
let isCall = true, isReturn = true, isTerminator = true, isBarrier = true,
    hasSideEffects = true, Uses = [R254, RJ, RG, RL, RO] in {
  def INDIRECT_TAIL_STATE
      : MMIXPseudoInstruction<
            (outs), (ins GPR64Op:$callee, variable_ops),
            "# INDIRECT_TAIL_STATE $callee">;
  def DIRECT_TAIL_STATE
      : MMIXPseudoInstruction<
            (outs), (ins MMIXCallTarget:$callee, variable_ops),
            "# DIRECT_TAIL_STATE $callee">;
  def MATERIALIZED_DIRECT_TAIL_STATE
      : MMIXPseudoInstruction<
            (outs), (ins MMIXCallTarget:$callee, GPR64Op:$scratch,
                         variable_ops),
            "# MATERIALIZED_DIRECT_TAIL_STATE $callee, $scratch">;
}

// Post-RA expansion keeps the complete terminal-call state on these final
// pseudos until MC lowering selects JMP/JMPB or GO. GO writes its unused link
// result to the reserved target scratch register.
let isCall = true, isReturn = true, isTerminator = true, isBarrier = true,
    hasSideEffects = true, Uses = [R254, RJ, RG, RL, RO] in {
  def PseudoDirectTail
      : MMIXPseudoInstruction<
            (outs), (ins MMIXJumpTarget:$callee, variable_ops), ""> {
    let Size = 4;
  }
  def PseudoMaterializedDirectTail
      : MMIXPseudoInstruction<
            (outs), (ins MMIXCallTarget:$callee, GPR64Op:$scratch,
                         variable_ops), ""> {
    let Size = 4;
    let Defs = [R255];
  }
  def PseudoIndirectTail
      : MMIXPseudoInstruction<
            (outs), (ins GPR64Op:$callee, variable_ops), ""> {
    let Size = 4;
    let Defs = [R255];
  }
}

// Direction is a layout property, so direct calls remain neutral until MC
// lowering. Indirect calls use the immediate-zero PUSHGO form.
let isCall = true, hasSideEffects = true, Uses = [R254, RG, RL, RO],
    Defs = [RJ, RL, RO, R251] in {
  def PseudoPUSHJ
      : MMIXPseudoInstruction<(outs),
                              (ins GPR64Op:$count,
                                   MMIXBranchTarget:$callee), ""> {
    let Size = 4;
  }
  def PseudoPUSHGO
      : MMIXPseudoInstruction<(outs),
                              (ins GPR64Op:$count, GPR64Op:$callee,
                                   MMIXUImm8:$offset), ""> {
    let Size = 4;
  }
  def PseudoDirectCall
      : MMIXPseudoInstruction<
            (outs), (ins GPR64Op:$count, MMIXBranchTarget:$callee,
                         GPR64Op:$scratch), ""> {
    let Size = 4;
  }
}

// Block layout determines whether MMIX must use a forward or backward opcode.
// Keep CodeGen branches direction-neutral and resolve them only during final
// MachineInstr-to-MCInst lowering. PseudoB's predicate is a direction-neutral
// index in architectural condition order: N, Z, P, OD, NN, NZ, NP, EV.
let isBranch = true, isTerminator = true in {
  def PseudoB
      : MMIXPseudoInstruction<
            (outs), (ins GPR64Op:$condition, MMIXBranchTarget:$target,
                         MMIXImm64:$predicate), ""> {
    let Size = 4;
  }
}

let isBranch = true, isTerminator = true, isBarrier = true in {
  def PseudoJMP
      : MMIXPseudoInstruction<(outs), (ins MMIXJumpTarget:$target), ""> {
    let Size = 4;
    let Pattern = [(br bb:$target)];
  }
}

def : Pat<(brcond i64:$condition, bb:$target),
          (PseudoB $condition, bb:$target, 5)>;

class MMIXBranchCCPat<PatFrag condition, Instruction compare,
                      bits<8> predicate>
    : Pat<(brcond (condition i64:$lhs, i64:$rhs), bb:$target),
          (PseudoB (compare $lhs, $rhs), bb:$target, predicate)>;

def : MMIXBranchCCPat<setlt,  CMP,  0>;
def : MMIXBranchCCPat<setle,  CMP,  6>;
def : MMIXBranchCCPat<setgt,  CMP,  2>;
def : MMIXBranchCCPat<setge,  CMP,  4>;
def : MMIXBranchCCPat<setult, CMPU, 0>;
def : MMIXBranchCCPat<setule, CMPU, 6>;
def : MMIXBranchCCPat<setugt, CMPU, 2>;
def : MMIXBranchCCPat<setuge, CMPU, 4>;
def : MMIXBranchCCPat<seteq,  CMPU, 1>;
def : MMIXBranchCCPat<setne,  CMPU, 5>;

class MMIXBranchCCImmPat<PatFrag condition, Instruction compare,
                         bits<8> predicate>
    : Pat<(brcond (condition i64:$lhs, mmix_uimm8:$rhs), bb:$target),
          (PseudoB (compare $lhs, mmix_uimm8:$rhs), bb:$target, predicate)>;

def : MMIXBranchCCImmPat<setlt,  CMPI,  0>;
def : MMIXBranchCCImmPat<setle,  CMPI,  6>;
def : MMIXBranchCCImmPat<setgt,  CMPI,  2>;
def : MMIXBranchCCImmPat<setge,  CMPI,  4>;
def : MMIXBranchCCImmPat<setult, CMPUI, 0>;
def : MMIXBranchCCImmPat<setule, CMPUI, 6>;
def : MMIXBranchCCImmPat<setugt, CMPUI, 2>;
def : MMIXBranchCCImmPat<setuge, CMPUI, 4>;
def : MMIXBranchCCImmPat<seteq,  CMPUI, 1>;
def : MMIXBranchCCImmPat<setne,  CMPUI, 5>;

def LOAD_IMM64
    : MMIXPseudoInstruction<(outs GPR64CodeGen:$dst),
                            (ins i64imm:$value), ""> {
  let Pattern = [(set i64:$dst, imm:$value)];
}

def : Pat<(f64 fpimm:$value),
          (COPY_TO_REGCLASS
              (LOAD_IMM64 (mmix_fpimm_bits f64:$value)), FPR64)>;

// An f32 register value is its raw binary32 encoding in the low tetra, not a
// numerically promoted binary64 value.
def : Pat<(f32 fpimm:$value),
          (COPY_TO_REGCLASS
              (LOAD_IMM64 (mmix_fpimm_bits f32:$value)), F32BitsCodeGen)>;

class MMIXAddressProducerPseudo<string Use>
    : MMIXPseudoInstruction<(outs GPR64CodeGen:$dst),
                            (ins i64imm:$symbol), ""> {
  string MMIXAddressProducerUse = Use;
  let Size = 16;
}

def LOAD_ADDR : MMIXAddressProducerPseudo<"static-address">;
// Keep the full TP-relative offset together until after register allocation.
def LOAD_TLS_ADDR
    : MMIXPseudoInstruction<(outs GPR64CodeGen:$dst),
                            (ins i64imm:$symbol), ""> {
  let Uses = [R230];
  let Size = 20;
  let hasSideEffects = false;
  let mayLoad = false;
  let mayStore = false;
}
def LOAD_CALL_ADDR
    : MMIXAddressProducerPseudo<"unresolved-direct-call">;

def : Pat<(MMIXload_addr tglobaladdr:$symbol),
          (LOAD_ADDR tglobaladdr:$symbol)>;
def : Pat<(MMIXload_addr texternalsym:$symbol),
          (LOAD_ADDR texternalsym:$symbol)>;
def : Pat<(MMIXload_addr tblockaddress:$symbol),
          (LOAD_ADDR tblockaddress:$symbol)>;
def : Pat<(MMIXload_addr tconstpool:$symbol),
          (LOAD_ADDR tconstpool:$symbol)>;
def : Pat<(MMIXload_addr tjumptable:$symbol),
          (LOAD_ADDR tjumptable:$symbol)>;
def : Pat<(MMIXload_call_addr tglobaladdr:$symbol),
          (LOAD_CALL_ADDR tglobaladdr:$symbol)>;
def : Pat<(MMIXload_call_addr texternalsym:$symbol),
          (LOAD_CALL_ADDR texternalsym:$symbol)>;

def SET_RD_ZERO : MMIXPseudoInstruction<(outs), (ins), ""> {
  let Defs = [RD];
}

let Defs = [R254], Uses = [R254], hasSideEffects = true in {
def ADJCALLSTACKDOWN
    : MMIXPseudoInstruction<(outs),
                            (ins i64imm:$amount, i64imm:$alignment),
                            "# ADJCALLSTACKDOWN $amount, $alignment"> {
  let Pattern = [(MMIXcallseq_start timm:$amount, timm:$alignment)];
}
def ADJCALLSTACKUP
    : MMIXPseudoInstruction<(outs),
                            (ins i64imm:$amount, i64imm:$alignment),
                            "# ADJCALLSTACKUP $amount, $alignment"> {
  let Pattern = [(MMIXcallseq_end timm:$amount, timm:$alignment)];
}
}

// These records describe expansion relationships without installing parser or
// printer aliases.  The canonical instruction records remain independently
// available for later encoding and lowering work.
class MMIXExpansionRelation<string Name, list<Instruction> Expansion> {
  string MMIXExpansionName = Name;
  list<Instruction> MMIXExpansionInstructions = Expansion;
}

class MMIXPseudoRelation<string PseudoName, list<Instruction> Expansion>
    : MMIXExpansionRelation<PseudoName, Expansion> {
  bit MMIXIsPseudo = true;
}

class MMIXAlias<string AliasName, list<Instruction> CanonicalInstructions>
    : MMIXExpansionRelation<AliasName, CanonicalInstructions> {
  bit MMIXIsAlias = true;
}

def MMIXSetAlias : MMIXAlias<"SET", [ORI]>;
def MMIXLDAAlias : MMIXAlias<"LDA", [ADDU, ADDUI]>;
