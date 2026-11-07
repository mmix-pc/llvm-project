//===--- MMIX.cpp - MMIX ToolChain Implementations ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MMIX.h"
#include "MMIXLLVMlibc.h"
#include "MMIXNewlib.h"
#include "MMIXPlatform.h"
#include "clang/Basic/DiagnosticDriver.h"
#include "clang/Driver/CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Job.h"
#include "clang/Driver/Tool.h"
#include "clang/Options/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace llvm::opt;

namespace {

class UnsupportedAssembler final : public Tool {
public:
  UnsupportedAssembler(const ToolChain &TC)
      : Tool("MMIX::UnsupportedAssembler", "MMIX external assembler", TC) {}

  bool hasIntegratedCPP() const override { return false; }

  void ConstructJob(Compilation &C, const JobAction &, const InputInfo &,
                    const InputInfoList &, const ArgList &,
                    const char *) const override {
    C.getDriver().Diag(diag::err_drv_clang_unsupported)
        << "external assembly for MMIX";
  }
};

static bool isLinkerPluginOption(StringRef Value) {
  return Value == "-plugin" || Value == "--plugin" ||
         Value.starts_with("-plugin=") || Value.starts_with("--plugin=");
}

static bool diagnoseUnsupportedLinkMode(Compilation &C, const ToolChain &TC,
                                        const ArgList &Args) {
  const Driver &D = C.getDriver();
  const bool IsHosted = !D.SysRoot.empty();
  const bool IsRelocatable = Args.hasArg(options::OPT_r);
  auto Diagnose = [&](StringRef Mode) {
    D.Diag(diag::err_drv_clang_unsupported) << Mode;
    return true;
  };

  if (Args.hasArg(options::OPT_shared))
    return Diagnose("shared linking for MMIX");
  if (Args.hasArg(options::OPT_dynamic, options::OPT_rdynamic))
    return Diagnose("dynamic linking for MMIX");
  if (Args.hasArg(options::OPT_pie, options::OPT_static_pie))
    return Diagnose("PIE linking for MMIX");
  if (const Arg *A = Args.getLastArg(
          options::OPT_fPIC, options::OPT_fno_PIC, options::OPT_fpic,
          options::OPT_fno_pic, options::OPT_fPIE, options::OPT_fno_PIE,
          options::OPT_fpie, options::OPT_fno_pie)) {
    if (A->getOption().matches(options::OPT_fPIC) ||
        A->getOption().matches(options::OPT_fpic) ||
        A->getOption().matches(options::OPT_fPIE) ||
        A->getOption().matches(options::OPT_fpie))
      return Diagnose("position-independent linking for MMIX");
  }
  if (TC.getLTOMode(Args) == LTOK_Thin)
    return Diagnose("ThinLTO linking for MMIX");
  if (IsRelocatable && TC.getLTOMode(Args) != LTOK_None)
    return Diagnose("LTO relocatable linking for MMIX");
  for (const Arg *A : Args.filtered(options::OPT_Wl_COMMA))
    for (StringRef Value : A->getValues())
      if (isLinkerPluginOption(Value))
        return Diagnose("linker plugin loading for MMIX");
  for (StringRef Value : Args.getAllArgValues(options::OPT_Xlinker))
    if (isLinkerPluginOption(Value))
      return Diagnose("linker plugin loading for MMIX");
  if (Args.hasArg(options::OPT_ld_path_EQ))
    return Diagnose("custom linker selection for MMIX");
  if (const Arg *A = Args.getLastArg(options::OPT_fuse_ld_EQ)) {
    if (StringRef(A->getValue()) != "lld")
      return Diagnose("non-lld linker selection for MMIX");
  }
  if (IsRelocatable) {
    if (Args.hasArg(options::OPT_rtlib_EQ, options::OPT_unwindlib_EQ))
      return Diagnose("runtime library selection for MMIX relocatable linking");
    return false;
  }
  if (IsHosted) {
    if (const Arg *A = Args.getLastArg(options::OPT_rtlib_EQ)) {
      Args.claimAllArgs(options::OPT_rtlib_EQ);
      if (StringRef(A->getValue()) != "compiler-rt")
        return Diagnose("non-compiler-rt runtime selection for MMIX");
    }
    if (const Arg *A = Args.getLastArg(options::OPT_unwindlib_EQ)) {
      Args.claimAllArgs(options::OPT_unwindlib_EQ);
      if (StringRef(A->getValue()) != "none" &&
          StringRef(A->getValue()) != "libunwind")
        return Diagnose("unwind library selection for MMIX");
    }
    return false;
  }
  if (Args.hasArg(options::OPT_rtlib_EQ, options::OPT_unwindlib_EQ))
    return Diagnose("runtime library selection for MMIX freestanding linking");
  if (!Args.hasArg(options::OPT_ffreestanding))
    return Diagnose("implicit hosted linking for MMIX");
  if (!Args.hasArg(options::OPT_nostdlib) ||
      !Args.hasArg(options::OPT_nostartfiles) ||
      !Args.hasArg(options::OPT_nodefaultlibs))
    return Diagnose("implicit runtime files for MMIX freestanding linking");

  return false;
}

static std::string getSysrootLibraryPath(const Driver &D) {
  SmallString<128> Path(D.SysRoot);
  llvm::sys::path::append(Path, "usr", "lib", "mmix");
  return std::string(Path);
}

static bool isDirectory(const ToolChain &TC, StringRef Path) {
  auto Status = TC.getVFS().status(Path);
  return Status && Status->isDirectory();
}

static bool isRegularFile(const ToolChain &TC, StringRef Path) {
  auto Status = TC.getVFS().status(Path);
  return Status && Status->isRegularFile();
}

class StaticLinker final : public Tool {
public:
  StaticLinker(const ToolChain &TC)
      : Tool("MMIX::StaticLinker", "MMIX static linker", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const ArgList &Args, const char *) const override {
    const ToolChain &TC = getToolChain();
    if (diagnoseUnsupportedLinkMode(C, TC, Args))
      return;

    const Driver &D = TC.getDriver();
    const bool IsRelocatable = Args.hasArg(options::OPT_r);
    const bool IsHosted = !IsRelocatable && !D.SysRoot.empty();
    const bool AddDefaultLibraries =
        IsHosted &&
        !Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs);
    const bool AddCXXRuntime =
        AddDefaultLibraries && TC.ShouldLinkCXXStdlib(Args);
    ToolChain::CStdlibType CStdlib = TC.GetCStdlibType(Args);

    if (IsHosted && !isDirectory(TC, D.SysRoot)) {
      D.Diag(diag::err_missing_sysroot) << D.SysRoot;
      return;
    }

    std::string LibraryPath;
    std::string DefaultScript;
    SmallVector<std::string, 3> StartFiles;
    std::string TerminationFile;
    SmallVector<std::string, 1> PlatformLibraries;
    std::string CXXStdlib;
    std::string CXXRuntime;
    std::string CRTBegin;
    std::string CRTEnd;
    std::string UnwindRuntime;
    std::string LibC;
    std::string Builtins;
    std::string Atomic;
    std::string StackProtector;
    bool InputsValid = true;

    if (IsHosted) {
      switch (CStdlib) {
      case ToolChain::CST_Newlib: {
        LibraryPath = getSysrootLibraryPath(D);
        if (!isDirectory(TC, LibraryPath)) {
          D.Diag(diag::err_drv_no_such_file) << LibraryPath;
          return;
        }
        mmix::ExecutionPlatform Platform = mmix::getExecutionPlatform();
        if (auto PlatformInputs = mmix::getNewlibExecutionPlatformInputs(
                Platform, TC, Args, LibraryPath)) {
          DefaultScript = std::move(PlatformInputs->DefaultLinkerScript);
          StartFiles = std::move(PlatformInputs->StartFiles);
          TerminationFile = std::move(PlatformInputs->TerminationFile);
          PlatformLibraries = std::move(PlatformInputs->ServiceLibraries);
        } else {
          InputsValid = false;
        }
        if (AddDefaultLibraries) {
          if (auto Path = mmix::getNewlibLibCPath(TC, LibraryPath))
            LibC = std::move(*Path);
          else
            InputsValid = false;
        }
        InputsValid &=
            mmix::validateNewlibExplicitLibraries(TC, Args, LibraryPath);
        break;
      }
      case ToolChain::CST_LLVMLibC: {
        mmix::LLVMlibcInstallation Installation =
            mmix::getLLVMlibcInstallation(D.SysRoot);
        LibraryPath = Installation.LibraryDirectory;
        mmix::ExecutionPlatformInputs PlatformInputs =
            mmix::getLLVMlibcExecutionPlatformInputs(
                mmix::getExecutionPlatform(), Args, Installation);
        DefaultScript = std::move(PlatformInputs.DefaultLinkerScript);
        StartFiles = std::move(PlatformInputs.StartFiles);
        PlatformLibraries = std::move(PlatformInputs.ServiceLibraries);
        InputsValid &= mmix::validateLLVMlibcRuntimeInputs(
            TC, Args, Installation);
        if (AddDefaultLibraries)
          LibC = Installation.LibC;
        break;
      }
      case ToolChain::CST_Picolibc:
      case ToolChain::CST_System:
        llvm_unreachable("unsupported MMIX C library survived validation");
      }

      if (AddDefaultLibraries) {
        if (AddCXXRuntime) {
          SmallString<128> StdlibPath(TC.getCompilerRTPath());
          llvm::sys::path::append(StdlibPath, "libc++.a");
          CXXStdlib = std::string(StdlibPath);
          if (!isRegularFile(TC, CXXStdlib)) {
            D.Diag(diag::err_drv_no_such_file) << CXXStdlib;
            InputsValid = false;
          }
          SmallString<128> Path(TC.getCompilerRTPath());
          llvm::sys::path::append(Path, "libc++abi.a");
          CXXRuntime = std::string(Path);
          if (!isRegularFile(TC, CXXRuntime)) {
            D.Diag(diag::err_drv_no_such_file) << CXXRuntime;
            InputsValid = false;
          }
          if (Args.getLastArgValue(options::OPT_unwindlib_EQ) != "none") {
            SmallString<128> UnwindPath(TC.getCompilerRTPath());
            llvm::sys::path::append(UnwindPath, "libunwind.a");
            UnwindRuntime = std::string(UnwindPath);
            if (!isRegularFile(TC, UnwindRuntime)) {
              D.Diag(diag::err_drv_no_such_file) << UnwindRuntime;
              InputsValid = false;
            }
          }
          // CRT lifecycle follows C++ default-runtime and startup suppression.
          if (!Args.hasArg(options::OPT_nostartfiles)) {
            CRTBegin = TC.getCompilerRT(Args, "crtbegin", ToolChain::FT_Object);
            CRTEnd = TC.getCompilerRT(Args, "crtend", ToolChain::FT_Object);
            InputsValid &= isRegularFile(TC, CRTBegin);
            InputsValid &= isRegularFile(TC, CRTEnd);
          }
        }
        Builtins = TC.getCompilerRT(Args, "builtins", ToolChain::FT_Static);
        Atomic = TC.getCompilerRT(Args, "atomic", ToolChain::FT_Static);
        StackProtector =
            TC.getCompilerRT(Args, "stack_protector", ToolChain::FT_Static);
        InputsValid &= isRegularFile(TC, Builtins);
        InputsValid &= isRegularFile(TC, Atomic);
        InputsValid &= isRegularFile(TC, StackProtector);
      }
    }

    if (!InputsValid)
      return;

    ArgStringList CmdArgs;
    CmdArgs.push_back("-m");
    CmdArgs.push_back("elf64mmix");
    if (IsRelocatable) {
      Args.claimAllArgs(options::OPT_r);
      CmdArgs.push_back("-r");
    } else {
      CmdArgs.push_back("-static");
    }
    Args.addAllArgs(CmdArgs, {options::OPT_L, options::OPT_s, options::OPT_t,
                              options::OPT_u_Group});
    if (IsHosted)
      CmdArgs.push_back(Args.MakeArgString(llvm::Twine("-L") + LibraryPath));
    if (!DefaultScript.empty()) {
      CmdArgs.push_back("-T");
      CmdArgs.push_back(Args.MakeArgString(DefaultScript));
    } else {
      Args.addAllArgs(CmdArgs, {options::OPT_T_Group});
    }
    for (const std::string &StartFile : StartFiles)
      CmdArgs.push_back(Args.MakeArgString(StartFile));
    if (!CRTBegin.empty())
      CmdArgs.push_back(Args.MakeArgString(CRTBegin));
    if (auto LTO = TC.getLTOMode(Args); LTO != LTOK_None)
      tools::addLTOOptions(TC, Args, CmdArgs, Output, Inputs,
                           LTO == LTOK_Thin);
    tools::AddLinkerInputs(TC, Inputs, Args, CmdArgs, JA);
    if (AddDefaultLibraries) {
      CmdArgs.push_back("--start-group");
      if (AddCXXRuntime) {
        CmdArgs.push_back(Args.MakeArgString(CXXStdlib));
        CmdArgs.push_back(Args.MakeArgString(CXXRuntime));
      }
      if (!UnwindRuntime.empty())
        CmdArgs.push_back(Args.MakeArgString(UnwindRuntime));
      CmdArgs.push_back(Args.MakeArgString(LibC));
      for (const std::string &PlatformLibrary : PlatformLibraries)
        CmdArgs.push_back(Args.MakeArgString(PlatformLibrary));
      CmdArgs.push_back(Args.MakeArgString(Builtins));
      CmdArgs.push_back(Args.MakeArgString(Atomic));
      CmdArgs.push_back(Args.MakeArgString(StackProtector));
      CmdArgs.push_back("--end-group");
    }
    if (!CRTEnd.empty())
      CmdArgs.push_back(Args.MakeArgString(CRTEnd));
    if (!TerminationFile.empty())
      CmdArgs.push_back(Args.MakeArgString(TerminationFile));
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());

    const char *Exec = Args.MakeArgString(TC.GetLinkerPath());
    C.addCommand(std::make_unique<Command>(
        JA, *this, ResponseFileSupport::AtFileCurCP(), Exec, CmdArgs, Inputs,
        Output));
  }
};

} // namespace

llvm::ExceptionHandling
MMIXToolChain::GetExceptionModel(const ArgList &) const {
  return llvm::ExceptionHandling::DwarfCFI;
}

MMIXToolChain::MMIXToolChain(const Driver &D, const llvm::Triple &Triple,
                             const ArgList &Args)
    : ToolChain(D, Triple, Args) {
  getProgramPaths().push_back(getDriver().Dir);
  (void)GetCStdlibType(Args);
  if (const Arg *A = Args.getLastArg(options::OPT_stdlib_EQ)) {
    StringRef Name = A->getValue();
    if (Name == "libstdc++")
      getDriver().Diag(diag::err_drv_unsupported_opt_for_target)
          << A->getAsString(Args) << getTripleString();
    else if (Name != "libc++" && Name != "platform")
      getDriver().Diag(diag::err_drv_invalid_stdlib_name)
          << A->getAsString(Args);
  }
}

ToolChain::RuntimeLibType
MMIXToolChain::GetRuntimeLibType(const ArgList &Args) const {
  if (const Arg *A = Args.getLastArg(options::OPT_rtlib_EQ)) {
    if (StringRef(A->getValue()) != "compiler-rt")
      getDriver().Diag(diag::err_drv_clang_unsupported)
          << "non-compiler-rt runtime selection for MMIX";
  }
  return ToolChain::RLT_CompilerRT;
}

ToolChain::CStdlibType
MMIXToolChain::GetCStdlibType(const ArgList &Args) const {
  const Arg *A = Args.getLastArg(options::OPT_cstdlib_EQ);
  if (!A)
    return ToolChain::CST_LLVMLibC;

  CStdlibType Type = ToolChain::GetCStdlibType(Args);
  if (Type == ToolChain::CST_Newlib || Type == ToolChain::CST_LLVMLibC)
    return Type;

  StringRef Name = A->getValue();
  bool IsKnownUnsupported = Type == ToolChain::CST_Picolibc ||
                            (Type == ToolChain::CST_System && Name == "system");
  if (IsKnownUnsupported && !DiagnosedUnsupportedCStdlib) {
    getDriver().Diag(diag::err_drv_unsupported_opt_for_target)
        << A->getAsString(Args) << getTripleString();
    DiagnosedUnsupportedCStdlib = true;
  }

  // Keep later target-private policy dispatch on a supported provider.
  return ToolChain::CST_LLVMLibC;
}

std::string
MMIXToolChain::ComputeEffectiveClangTriple(const ArgList &Args, BoundArch BA,
                                           types::ID InputType) const {
  StringRef RequestedTriple = getDriver().getTargetTriple();
  if (const Arg *A = Args.getLastArg(options::OPT_target))
    RequestedTriple = A->getValue();
  if (RequestedTriple == "mmix-unknown-elf")
    return "mmix-unknown-unknown";
  return ComputeLLVMTriple(Args, BA, InputType);
}

std::string MMIXToolChain::getCompilerRTPath() const {
  SmallString<128> Path(getDriver().ResourceDir);
  llvm::sys::path::append(Path, "lib", "mmix-unknown-unknown");
  return std::string(Path);
}

std::string MMIXToolChain::getCompilerRT(const ArgList &Args,
                                         StringRef Component, FileType Type,
                                         bool IsFortran) const {
  if ((Type != ToolChain::FT_Static && Type != ToolChain::FT_Object) || IsFortran)
    return ToolChain::getCompilerRT(Args, Component, Type, IsFortran);

  SmallString<128> Path(getCompilerRTPath());
  llvm::sys::path::append(
      Path, buildCompilerRTBasename(Args, Component, Type,
                                    /*AddArch=*/false, IsFortran));
  if (!isRegularFile(*this, Path))
    getDriver().Diag(diag::err_drv_no_such_file) << Path;
  return std::string(Path);
}

void MMIXToolChain::AddClangCXXStdlibIncludeArgs(const ArgList &DriverArgs,
                                               ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(options::OPT_nostdinc, options::OPT_nostdincxx,
                       options::OPT_nostdlibinc))
    return;
  SmallString<128> Path(getDriver().ResourceDir);
  llvm::sys::path::append(Path, "include", "mmix-unknown-unknown", "c++", "v1");
  // libc++ and its provider-specific __config_site are installed together.
  // Do not fall back to host or build-tree headers for an incomplete resource.
  addSystemInclude(DriverArgs, CC1Args, Path);
}

void MMIXToolChain::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                              ArgStringList &CC1Args) const {
  // The driver owns the target's system include paths. In particular, an
  // absent sysroot must not enable the frontend's legacy host directories.
  CC1Args.push_back("-nostdsysteminc");
  const Driver &D = getDriver();
  if (D.SysRoot.empty() || DriverArgs.hasArg(options::OPT_nostdinc))
    return;

  if (!DriverArgs.hasArg(options::OPT_nostdlibinc)) {
    SmallString<128> UnwindInclude(D.ResourceDir);
    llvm::sys::path::append(UnwindInclude, "include", "mmix-unknown-unknown");
    addSystemInclude(DriverArgs, CC1Args, UnwindInclude);
  }
  if (!isDirectory(*this, D.SysRoot)) {
    D.Diag(diag::err_missing_sysroot) << D.SysRoot;
    return;
  }

  if (!DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    SmallString<128> ResourceInclude(D.ResourceDir);
    llvm::sys::path::append(ResourceInclude, "include");
    addSystemInclude(DriverArgs, CC1Args, ResourceInclude);
  }

  switch (GetCStdlibType(DriverArgs)) {
  case ToolChain::CST_Newlib:
    mmix::addNewlibSystemIncludeArgs(*this, DriverArgs, CC1Args);
    return;
  case ToolChain::CST_LLVMLibC:
    mmix::addLLVMlibcSystemIncludeArgs(
        *this, DriverArgs, CC1Args,
        mmix::getLLVMlibcInstallation(D.SysRoot));
    return;
  case ToolChain::CST_Picolibc:
  case ToolChain::CST_System:
    llvm_unreachable("unsupported MMIX C library survived validation");
  }
}

void MMIXToolChain::addClangTargetOptions(const ArgList &DriverArgs,
                                          ArgStringList &, BoundArch,
                                          Action::OffloadKind) const {
  auto DiagnoseUnsupported = [&](const Arg *A) {
    getDriver().Diag(diag::err_drv_unsupported_opt_for_target)
        << A->getAsString(DriverArgs) << getTripleString();
  };

  if (const Arg *A = DriverArgs.getLastArg(
          options::OPT_mstack_protector_guard_symbol_EQ))
    DiagnoseUnsupported(A);

  if (const Arg *A =
          DriverArgs.getLastArg(options::OPT_fstack_clash_protection,
                                options::OPT_fno_stack_clash_protection);
      A && A->getOption().matches(options::OPT_fstack_clash_protection))
    DiagnoseUnsupported(A);

  if (const Arg *A = DriverArgs.getLastArg(options::OPT_fsplit_stack,
                                           options::OPT_fno_split_stack);
      A && A->getOption().matches(options::OPT_fsplit_stack))
    DiagnoseUnsupported(A);

  if (const Arg *A = DriverArgs.getLastArg(
          options::OPT_fprofile_instr_generate,
          options::OPT_fprofile_instr_generate_EQ,
          options::OPT_fprofile_generate, options::OPT_fprofile_generate_EQ,
          options::OPT_fcs_profile_generate,
          options::OPT_fcs_profile_generate_EQ, options::OPT_coverage,
          options::OPT_fprofile_arcs, options::OPT_ftest_coverage))
    DiagnoseUnsupported(A);

  if (const Arg *A = DriverArgs.getLastArg(
          options::OPT_funwind_tables, options::OPT_funwind_tables_EQ,
          options::OPT_fasynchronous_unwind_tables))
    DiagnoseUnsupported(A);

  if (const Arg *A = DriverArgs.getLastArg(
          options::OPT_gsplit_dwarf, options::OPT_gsplit_dwarf_EQ,
          options::OPT_gno_split_dwarf);
      A && !A->getOption().matches(options::OPT_gno_split_dwarf))
    DiagnoseUnsupported(A);
}

Tool *MMIXToolChain::buildAssembler() const {
  return new UnsupportedAssembler(*this);
}

Tool *MMIXToolChain::buildLinker() const {
  return new StaticLinker(*this);
}
