//===-- MMIXMCExpr.h - MMIX expression validation -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MMIX_MCTARGETDESC_MMIXMCEXPR_H
#define LLVM_LIB_TARGET_MMIX_MCTARGETDESC_MMIXMCEXPR_H

namespace llvm {
class MCContext;
class MCExpr;
class MCInst;
class MCSubtargetInfo;
class MCValue;

namespace MMIX {
// Decode a normalized symbol-plus-signed-addend without folding the symbol.
bool getTPRELValue(const MCExpr &Expr, MCValue &Value);
bool validateTPRELInstruction(const MCInst &Inst, const MCSubtargetInfo &STI,
                              MCContext &Ctx);
} // namespace MMIX
} // namespace llvm

#endif
