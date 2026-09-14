//===--- MMIXLinux.cpp - MMIX Linux ToolChain -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MMIXLinux.h"
#include "clang/Basic/DiagnosticDriver.h"
#include "clang/Driver/CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Job.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/Types.h"
#include "clang/Options/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace llvm::opt;

namespace {
bool diagnoseMissing(const ToolChain &TC, StringRef Path, bool Directory) {
  auto Status = TC.getVFS().status(Path);
  if (Status && (Directory ? Status->isDirectory() : Status->isRegularFile()))
    return false;
  TC.getDriver().Diag(diag::err_drv_no_such_file) << Path;
  return true;
}

class UnavailableTool final : public Tool {
  const char *Operation;

public:
  UnavailableTool(const ToolChain &TC, const char *Operation)
      : Tool("MMIXLinux::Unavailable", Operation, TC), Operation(Operation) {}

  bool hasIntegratedCPP() const override { return false; }
  void ConstructJob(Compilation &C, const JobAction &, const InputInfo &,
                    const InputInfoList &, const ArgList &,
                    const char *) const override {
    C.getDriver().Diag(diag::err_drv_clang_unsupported) << Operation;
  }
};

class Linker final : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("MMIXLinux::Linker", "linker", TC) {}
  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }
  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const ArgList &Args, const char *) const override {
    const auto &TC = static_cast<const MMIXLinuxToolChain &>(getToolChain());
    const Driver &D = TC.getDriver();
    auto Reject = [&](StringRef Mode) {
      D.Diag(diag::err_drv_clang_unsupported) << Mode;
    };
    // Full Linux LTO qualification follows the ordinary static link boundary.
    if (TC.getLTOMode(Args) != LTOK_None) {
      Reject("LTO linking for MMIX Linux");
      return;
    }
    for (const InputInfo &Input : Inputs) {
      if (types::isLLVMIR(Input.getType())) {
        Reject("LTO linking for MMIX Linux");
        return;
      }
    }
    if (Args.hasArg(options::OPT_ld_path_EQ)) {
      Reject("custom linker selection for MMIX Linux");
      return;
    }
    // Do not let forwarded mode switches override the reviewed static profile.
    for (const Arg *A :
         Args.filtered(options::OPT_Wl_COMMA, options::OPT_Xlinker)) {
      for (StringRef V : A->getValues()) {
        StringRef Option = V.split('=').first;
        if (Option == "-shared" || Option == "--shared" || Option == "-pie" ||
            Option == "--pie" || Option == "-Bdynamic" || Option == "-dy" ||
            Option == "--dynamic-linker" || Option == "-dynamic-linker" ||
            Option == "-I" || V.starts_with("-I") || Option == "-plugin" ||
            Option == "--plugin" || Option == "-r" ||
            Option == "--relocatable" || Option == "-m" ||
            V.starts_with("-m") || Option == "--emulation") {
          D.Diag(diag::err_drv_unsupported_opt_for_target)
              << A->getAsString(Args) << TC.getTripleString();
          return;
        }
      }
    }

    const bool Relocatable = Args.hasArg(options::OPT_r);
    const bool StartFiles =
        !Relocatable &&
        !Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles);
    const bool DefaultLibs =
        !Relocatable &&
        !Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs);
    ArgStringList CmdArgs{"-m", "elf64mmix_linux"};
    CmdArgs.push_back(Relocatable ? "-r" : "-static");
    if (!Relocatable) {
      CmdArgs.push_back("--no-dynamic-linker");
      CmdArgs.push_back("--eh-frame-hdr");
      // MMIX Linux loads segments with 8192-byte page congruence.
      CmdArgs.append(
          {"-z", "max-page-size=8192", "-z", "common-page-size=8192"});
    }
    if (!D.SysRoot.empty())
      CmdArgs.push_back(Args.MakeArgString("--sysroot=" + D.SysRoot));
    Args.ClaimAllArgs(options::OPT_r);
    Args.ClaimAllArgs(options::OPT_static);
    Args.ClaimAllArgs(options::OPT_no_pie);
    Args.ClaimAllArgs(options::OPT_g_Group);
    Args.ClaimAllArgs(options::OPT_emit_llvm);
    Args.ClaimAllArgs(options::OPT_w);
    Args.addAllArgs(CmdArgs, {options::OPT_L, options::OPT_s, options::OPT_t,
                              options::OPT_u_Group});
    TC.AddFilePathLibArgs(Args, CmdArgs);
    if (StartFiles) {
      CmdArgs.push_back(Args.MakeArgString(TC.getSysrootFile("crt1.o")));
      CmdArgs.push_back(
          TC.getCompilerRTArgString(Args, "crtbegin", ToolChain::FT_Object));
    }
    tools::addLinkerCompressDebugSectionsOption(TC, Args, CmdArgs);
    tools::AddLinkerInputs(TC, Inputs, Args, CmdArgs, JA);
    if (DefaultLibs) {
      CmdArgs.push_back("--start-group");
      if (TC.ShouldLinkCXXStdlib(Args)) {
        TC.AddCXXStdlibLibArgs(Args, CmdArgs);
        CmdArgs.push_back(Args.MakeArgString(TC.getSysrootFile("libm.a")));
      }
      if (TC.GetUnwindLibType(Args) == ToolChain::UNW_CompilerRT)
        CmdArgs.push_back(Args.MakeArgString(TC.getSysrootFile("libunwind.a")));
      if (!Args.hasArg(options::OPT_nolibc))
        CmdArgs.push_back(Args.MakeArgString(TC.getSysrootFile("libc.a")));
      CmdArgs.push_back(TC.getCompilerRTArgString(Args, "builtins"));
      CmdArgs.push_back("--end-group");
    }
    if (StartFiles)
      CmdArgs.push_back(
          TC.getCompilerRTArgString(Args, "crtend", ToolChain::FT_Object));
    Args.addAllArgs(CmdArgs, {options::OPT_T_Group});
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
    const char *Exec = Args.MakeArgString(TC.GetLinkerPath());
    if (D.getDiags().hasErrorOccurred())
      return;
    C.addCommand(std::make_unique<Command>(JA, *this,
                                           ResponseFileSupport::AtFileCurCP(),
                                           Exec, CmdArgs, Inputs, Output));
  }
};
} // namespace

MMIXLinuxToolChain::MMIXLinuxToolChain(const Driver &D,
                                       const llvm::Triple &Triple,
                                       const ArgList &Args)
    : ToolChain(D, Triple, Args) {
  // Linux's constructor probes GCC and installs distribution search paths.
  // FIXME: Add native MMIX Linux system paths without implicit host fallback
  // when cross-compiling, GCC discovery, or changes to LLVM provider defaults.
  getFilePaths().clear();
  getLibraryPaths().clear();
  getProgramPaths().push_back(D.Dir);
  getLibraryPaths().push_back(getCompilerRTPath());
  if (!D.SysRoot.empty()) {
    SmallString<128> Path(D.SysRoot);
    llvm::sys::path::append(Path, "usr", "lib");
    getFilePaths().push_back(std::string(Path));
  }

  auto Reject = [&](const Arg *A) {
    D.Diag(diag::err_drv_unsupported_opt_for_target)
        << A->getAsString(Args) << getTripleString();
  };
  auto RequireProvider = [&](OptSpecifier Option, StringRef Provider,
                             bool AllowPlatform = false) {
    if (const Arg *A = Args.getLastArg(Option)) {
      A->claim();
      StringRef Value = A->getValue();
      if (Value != Provider && !(AllowPlatform && Value == "platform"))
        Reject(A);
    }
  };
  RequireProvider(options::OPT_cstdlib_EQ, "llvm-libc");
  RequireProvider(options::OPT_rtlib_EQ, "compiler-rt", true);
  RequireProvider(options::OPT_stdlib_EQ, "libc++", true);
  RequireProvider(options::OPT_fuse_ld_EQ, "lld");
  if (const Arg *A = Args.getLastArg(options::OPT_unwindlib_EQ)) {
    A->claim();
    StringRef Value = A->getValue();
    if (Value != "none" && Value != "libunwind" && Value != "platform")
      Reject(A);
  }
  if (const Arg *A = Args.getLastArg(
          options::OPT_fPIC, options::OPT_fpic, options::OPT_fPIE,
          options::OPT_fpie, options::OPT_fno_PIC, options::OPT_fno_pic,
          options::OPT_fno_PIE, options::OPT_fno_pie)) {
    if (A->getOption().matches(options::OPT_fPIC) ||
        A->getOption().matches(options::OPT_fpic) ||
        A->getOption().matches(options::OPT_fPIE) ||
        A->getOption().matches(options::OPT_fpie))
      Reject(A);
  }
  for (const Arg *A : Args.filtered(
           options::OPT_shared, options::OPT_dynamic, options::OPT_rdynamic,
           options::OPT_pie, options::OPT_static_pie, options::OPT_pthread,
           options::OPT_gcc_toolchain, options::OPT_gcc_install_dir_EQ,
           options::OPT_gcc_triple_EQ))
    Reject(A);
}

ToolChain::UnwindLibType
MMIXLinuxToolChain::GetUnwindLibType(const ArgList &Args) const {
  const Arg *A = Args.getLastArg(options::OPT_unwindlib_EQ);
  if (A && StringRef(A->getValue()) == "none")
    return UNW_None;
  // libc++ retains exception paths even when the application disables them.
  if ((A && StringRef(A->getValue()) == "libunwind") ||
      getDriver().CCCIsCXX() ||
      Args.hasFlag(options::OPT_fexceptions, options::OPT_fno_exceptions,
                   false))
    return UNW_CompilerRT;
  return UNW_None;
}

std::string MMIXLinuxToolChain::getSysrootFile(StringRef Name) const {
  if (getDriver().SysRoot.empty()) {
    getDriver().Diag(diag::err_drv_clang_unsupported)
        << "implicit system resources without --sysroot for MMIX Linux";
    return {};
  }
  SmallString<128> Path(getDriver().SysRoot);
  llvm::sys::path::append(Path, "usr", "lib", Name);
  diagnoseMissing(*this, Path, /*Directory=*/false);
  return std::string(Path);
}

void MMIXLinuxToolChain::AddCXXStdlibLibArgs(const ArgList &Args,
                                             ArgStringList &CmdArgs) const {
  CmdArgs.push_back(Args.MakeArgString(getSysrootFile("libc++.a")));
  CmdArgs.push_back(Args.MakeArgString(getSysrootFile("libc++abi.a")));
}

void MMIXLinuxToolChain::AddClangSystemIncludeArgs(
    const ArgList &Args, ArgStringList &CC1Args) const {
  if (Args.hasArg(options::OPT_nostdinc))
    return;
  const Driver &D = getDriver();
  if (!Args.hasArg(options::OPT_nobuiltininc)) {
    SmallString<128> Path(D.ResourceDir);
    llvm::sys::path::append(Path, "include");
    if (!diagnoseMissing(*this, Path, /*Directory=*/true))
      addSystemInclude(Args, CC1Args, Path);
  }
  // Without a sysroot, freestanding compilation can still use builtin headers.
  if (D.SysRoot.empty() || Args.hasArg(options::OPT_nostdlibinc))
    return;
  SmallString<128> Path(D.SysRoot);
  llvm::sys::path::append(Path, "usr", "include");
  if (!diagnoseMissing(*this, Path, /*Directory=*/true))
    addExternCSystemInclude(Args, CC1Args, Path);
}

void MMIXLinuxToolChain::AddClangCXXStdlibIncludeArgs(
    const ArgList &Args, ArgStringList &CC1Args) const {
  const Driver &D = getDriver();
  if (D.SysRoot.empty() ||
      Args.hasArg(options::OPT_nostdinc, options::OPT_nostdlibinc,
                  options::OPT_nostdincxx))
    return;
  SmallString<128> Path(D.SysRoot);
  llvm::sys::path::append(Path, "usr", "include", "c++", "v1");
  SmallString<128> Config(Path);
  llvm::sys::path::append(Config, "__config_site");
  // The generated configuration belongs to this libc++ installation, not to
  // a host or bare-metal resource directory with otherwise matching headers.
  if (!diagnoseMissing(*this, Config, /*Directory=*/false))
    addSystemInclude(Args, CC1Args, Path);
}

std::string MMIXLinuxToolChain::getCompilerRTPath() const {
  SmallString<128> Path(getDriver().ResourceDir);
  llvm::sys::path::append(Path, "lib", "mmix-unknown-linux");
  return std::string(Path);
}

std::string MMIXLinuxToolChain::getCompilerRT(const ArgList &Args,
                                           StringRef Component, FileType Type,
                                           bool IsFortran) const {
  SmallString<128> Path(getCompilerRTPath());
  llvm::sys::path::append(
      Path, buildCompilerRTBasename(Args, Component, Type,
                                   /*AddArch=*/false, IsFortran));
  diagnoseMissing(*this, Path, /*Directory=*/false);
  return std::string(Path);
}

Tool *MMIXLinuxToolChain::buildAssembler() const {
  return new UnavailableTool(*this, "external assembly for MMIX Linux");
}

Tool *MMIXLinuxToolChain::buildLinker() const {
  return new Linker(*this);
}
