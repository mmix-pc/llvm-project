//===-- MMIXMCAsmInfo.h - MMIX asm properties ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MMIX_MCTARGETDESC_MMIXMCASMINFO_H
#define LLVM_LIB_TARGET_MMIX_MCTARGETDESC_MMIXMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {

class MCSpecifierExpr;
class MCTargetOptions;
class Triple;

class MMIXMCAsmInfo : public MCAsmInfoELF {
public:
  explicit MMIXMCAsmInfo(const Triple &TT, const MCTargetOptions &Options);

  void printSpecifierExpr(raw_ostream &OS,
                          const MCSpecifierExpr &Expr) const override;
  bool evaluateAsRelocatableImpl(const MCSpecifierExpr &Expr, MCValue &Value,
                                 const MCAssembler *Asm) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_MMIX_MCTARGETDESC_MMIXMCASMINFO_H
