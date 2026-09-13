//===--- MMIX.h - Declare MMIX target feature support ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MMIX TargetInfo object.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_MMIX_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_MMIX_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY MMIXTargetInfo : public TargetInfo {
  bool HasBase = true;
  bool HasSystem = true;
  bool HasCache = true;
  bool HasVirtualMemory = true;

public:
  MMIXTargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    BoolWidth = BoolAlign = 8;
    ShortWidth = ShortAlign = 16;
    IntWidth = IntAlign = 32;
    LongWidth = LongAlign = 64;
    LongLongWidth = LongLongAlign = 64;
    Int128Align = 64;
    FloatWidth = FloatAlign = 32;
    DoubleWidth = DoubleAlign = 64;
    LongDoubleWidth = LongDoubleAlign = 64;
    PointerWidth = PointerAlign = 64;
    SuitableAlign = 64;
    DefaultAlignForAttributeAligned = 64;
    MinGlobalAlign = 32;
    MaxAlignedAttribute = 32768 * 8;

    HasMustTail = false;
    TLSSupported = false;
    MaxAtomicPromoteWidth = 0;
    MaxAtomicInlineWidth = 64;

    UseBitFieldTypeAlignment = false;
    UseZeroLengthBitfieldAlignment = true;
    ZeroLengthBitfieldBoundary = 64;

    SizeType = UnsignedLong;
    PtrDiffType = IntPtrType = SignedLong;
    IntMaxType = Int64Type = SignedLong;
    Int16Type = SignedShort;
    WCharType = SignedInt;
    WIntType = UnsignedInt;

    LongDoubleFormat = &llvm::APFloat::IEEEdouble();
    resetDataLayout();
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  bool
  initFeatureMap(llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
                 StringRef CPU,
                 const std::vector<std::string> &FeatureVec) const override;

  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override;

  bool hasFeature(StringRef Feature) const override;

  bool hasInt128Type() const override { return true; }

  int getEHDataRegisterNumber(unsigned RegNo) const override {
    // DWARF numbers for the exception pointer ($231) and selector ($232).
    return RegNo < 2 ? 7 + RegNo : -1;
  }

  // LLVM libc uses dependent bounded _BitInt aliases in vector support
  // headers. CodeGen retains its separate target-owned _BitInt boundary.
  bool hasBitIntType() const override { return true; }
  size_t getMaxBitIntWidth() const override { return 64; }

  bool isValidCPUName(StringRef Name) const override;

  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;

  bool setCPU(StringRef Name) override { return isValidCPUName(Name); }

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    return {};
  }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::VoidPtrBuiltinVaList;
  }

  ArrayRef<const char *> getGCCRegNames() const override;

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override;

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override;

  std::string convertConstraint(const char *&Constraint) const override;

  std::string_view getClobbers() const override { return ""; }

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override {
    return CC == CC_C ? CCCR_OK : CCCR_Error;
  }
};

} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_MMIX_H
