//===-- MMIXMCExpr.cpp - MMIX expression validation ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MMIXMCExpr.h"
#include "MMIXBaseInfo.h"
#include "MMIXMCTargetDesc.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Casting.h"

using namespace llvm;

bool MMIX::getTPRELValue(const MCExpr &Expr, MCValue &Value) {
  const MCExpr *Symbol = &Expr;
  int64_t Addend = 0;
  if (const auto *Binary = dyn_cast<MCBinaryExpr>(&Expr)) {
    const auto *Constant = dyn_cast<MCConstantExpr>(Binary->getRHS());
    if (Binary->getOpcode() != MCBinaryExpr::Add || !Constant)
      return false;
    Symbol = Binary->getLHS();
    Addend = Constant->getValue();
  }
  const auto *Ref = dyn_cast<MCSymbolRefExpr>(Symbol);
  if (!Ref || Ref->getKind())
    return false;
  Value = MCValue::get(&Ref->getSymbol(), nullptr, Addend);
  return true;
}

bool MMIX::validateTPRELInstruction(const MCInst &Inst,
                                    const MCSubtargetInfo &STI,
                                    MCContext &Ctx) {
  for (unsigned I = 0; I != Inst.getNumOperands(); ++I) {
    const MCOperand &Operand = Inst.getOperand(I);
    if (!Operand.isExpr())
      continue;
    const auto *Expr = dyn_cast<MCSpecifierExpr>(Operand.getExpr());
    if (!Expr || !MMIXII::isTPRELSpecifier(Expr->getSpecifier()))
      continue;
    auto Error = [&](const char *Message) {
      Ctx.reportError(Expr->getLoc(), Message);
      return false;
    };
    if (!STI.getTargetTriple().isOSLinux() ||
        Ctx.getAsmInfo().getOutputAssemblerDialect() !=
            MMIXII::CanonicalAsmVariant)
      return Error("MMIX TLS expressions require Linux canonical assembly");

    unsigned Opcode;
    switch (Expr->getSpecifier()) {
    case MMIXII::S_TPREL_LO:
      Opcode = MMIX::SETL;
      break;
    case MMIXII::S_TPREL_ML:
      Opcode = MMIX::INCML;
      break;
    case MMIXII::S_TPREL_MH:
      Opcode = MMIX::INCMH;
      break;
    case MMIXII::S_TPREL_HI:
      Opcode = MMIX::INCH;
      break;
    default:
      llvm_unreachable("not a TPREL specifier");
    }
    if (Inst.getOpcode() != Opcode || I != Inst.getNumOperands() - 1)
      return Error(
          "MMIX TLS modifier does not match the instruction's wyde operand");
    MCValue Value;
    if (!getTPRELValue(*Expr->getSubExpr(), Value))
      return Error(
          "MMIX TLS expression requires one symbol plus a signed addend");
  }
  return true;
}
