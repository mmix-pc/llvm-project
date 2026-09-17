//===--- MMIXLinux.h - MMIX Linux ToolChain --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MMIXLINUX_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MMIXLINUX_H

#include "clang/Driver/ToolChain.h"

namespace clang::driver::toolchains {

class LLVM_LIBRARY_VISIBILITY MMIXLinuxToolChain final : public ToolChain {
public:
  MMIXLinuxToolChain(const Driver &D, const llvm::Triple &Triple,
                     const llvm::opt::ArgList &Args);

  bool isPICDefault() const override { return false; }
  bool isPIEDefault(const llvm::opt::ArgList &) const override { return false; }
  bool isPICDefaultForced() const override { return false; }
  bool HasNativeLLVMSupport() const override { return true; }
  // Native Linux system and environment search is not implemented yet.
  bool isCrossCompiling() const override { return true; }
  bool SupportsProfiling() const override { return false; }
  llvm::ExceptionHandling
  GetExceptionModel(const llvm::opt::ArgList &) const override;
  const char *getDefaultLinker() const override { return "ld.lld"; }
  RuntimeLibType GetDefaultRuntimeLibType() const override {
    return RLT_CompilerRT;
  }
  RuntimeLibType GetRuntimeLibType(const llvm::opt::ArgList &) const override {
    return RLT_CompilerRT;
  }
  CStdlibType GetCStdlibType(const llvm::opt::ArgList &) const override {
    return CST_LLVMLibC;
  }
  CXXStdlibType GetDefaultCXXStdlibType() const override { return CST_Libcxx; }
  CXXStdlibType GetCXXStdlibType(const llvm::opt::ArgList &) const override {
    return CST_Libcxx;
  }
  UnwindLibType GetUnwindLibType(const llvm::opt::ArgList &Args) const override;
  std::string getSysrootFile(StringRef Name) const;
  void AddCXXStdlibLibArgs(const llvm::opt::ArgList &Args,
                           llvm::opt::ArgStringList &CmdArgs) const override;
  std::string getCompilerRTPath() const override;
  std::string getCompilerRT(const llvm::opt::ArgList &Args,
                            llvm::StringRef Component,
                            FileType Type = ToolChain::FT_Static,
                            bool IsFortran = false) const override;
  void
  AddClangSystemIncludeArgs(const llvm::opt::ArgList &Args,
                            llvm::opt::ArgStringList &CC1Args) const override;
  void AddClangCXXStdlibIncludeArgs(
      const llvm::opt::ArgList &Args,
      llvm::opt::ArgStringList &CC1Args) const override;

protected:
  Tool *buildAssembler() const override;
  Tool *buildLinker() const override;
};

} // namespace clang::driver::toolchains

#endif
