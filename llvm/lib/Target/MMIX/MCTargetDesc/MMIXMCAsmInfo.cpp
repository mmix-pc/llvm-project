//===-- MMIXMCAsmInfo.cpp - MMIX asm properties ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MMIXMCAsmInfo.h"
#include "MMIXBaseInfo.h"
#include "MMIXMCExpr.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

MMIXMCAsmInfo::MMIXMCAsmInfo(const Triple &TT, const MCTargetOptions &Options)
    : MCAsmInfoELF(Options) {
  CodePointerSize = 8;
  CalleeSaveStackSlotSize = 8;
  CommentString = "#";
  Data8bitsDirective = "\t.byte\t";
  Data16bitsDirective = "\t.2byte\t";
  Data32bitsDirective = "\t.4byte\t";
  Data64bitsDirective = "\t.8byte\t";
  ZeroDirective = "\t.space\t";
  MinInstAlignment = 4;
  AllowDigitAtStartOfIdentifier = true;
  UsesELFSectionDirectiveForBSS = true;
  IsLittleEndian = false;
  SupportsDebugInformation =
      Options.OutputAsmVariant.value_or(MMIXII::CanonicalAsmVariant) !=
      MMIXII::MMIXALAsmVariant;
  ExceptionsType = ExceptionHandling::DwarfCFI;
}

void MMIXMCAsmInfo::printSpecifierExpr(raw_ostream &OS,
                                       const MCSpecifierExpr &Expr) const {
  switch (Expr.getSpecifier()) {
  case MMIXII::S_GETA:
    OS << "%geta(";
    break;
  case MMIXII::S_TPREL_LO:
    OS << "%tprel_lo(";
    break;
  case MMIXII::S_TPREL_ML:
    OS << "%tprel_ml(";
    break;
  case MMIXII::S_TPREL_MH:
    OS << "%tprel_mh(";
    break;
  case MMIXII::S_TPREL_HI:
    OS << "%tprel_hi(";
    break;
  default:
    llvm_unreachable("unknown MMIX expression specifier");
  }
  printExpr(OS, *Expr.getSubExpr());
  OS << ')';
}

bool MMIXMCAsmInfo::evaluateAsRelocatableImpl(const MCSpecifierExpr &Expr,
                                              MCValue &Value,
                                              const MCAssembler *Asm) const {
  if (MMIXII::isTPRELSpecifier(Expr.getSpecifier()))
    return MMIX::getTPRELValue(*Expr.getSubExpr(), Value);
  return MCAsmInfoELF::evaluateAsRelocatableImpl(Expr, Value, Asm);
}
