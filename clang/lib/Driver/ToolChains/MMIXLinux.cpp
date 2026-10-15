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
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace llvm::opt;

llvm::ExceptionHandling
MMIXLinuxToolChain::GetExceptionModel(const ArgList &) const {
  return llvm::ExceptionHandling::DwarfCFI;
}

namespace {
bool requestsDynamicLink(const ArgList &Args) {
  if (Args.hasArg(options::OPT_shared) ||
      Args.hasFlag(options::OPT_pie, options::OPT_no_pie, false))
    return true;
  for (const Arg *A :
       Args.filtered(options::OPT_Wl_COMMA, options::OPT_Xlinker))
    for (StringRef V : A->getValues())
      if (V == "-Bdynamic")
        return true;
  return false;
}

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
    // The C-only package must not select single-thread C++ or unwind archives.
    if (Args.hasArg(options::OPT_pthread) && !TC.hasThreadedCXXProfile() &&
        !TC.hasSharedCProfile() &&
        (TC.ShouldLinkCXXStdlib(Args) ||
         TC.GetUnwindLibType(Args) != ToolChain::UNW_None)) {
      Reject("C++ or unwind runtime linking with MMIX Linux pthread");
      return;
    }
    const auto LTOMode = TC.getLTOMode(Args);
    if (LTOMode == LTOK_Thin) {
      Reject("ThinLTO linking for MMIX Linux");
      return;
    }
    bool HasIRInput = false;
    for (const InputInfo &Input : Inputs)
      HasIRInput |= types::isLLVMIR(Input.getType());
    const bool Relocatable = Args.hasArg(options::OPT_r);
    if (Relocatable && (LTOMode != LTOK_None || HasIRInput)) {
      Reject("LTO relocatable linking for MMIX Linux");
      return;
    }
    if (Args.hasArg(options::OPT_ld_path_EQ)) {
      Reject("custom linker selection for MMIX Linux");
      return;
    }
    const bool Shared = Args.hasArg(options::OPT_shared);
    const bool PIE = Args.hasFlag(options::OPT_pie, options::OPT_no_pie, false);
    bool DynamicInputs = false;
    // Mode selection belongs to the Driver, except the explicit -Bdynamic
    // selector for dynamic ET_EXEC. Do not allow forwarded role overrides.
    for (const Arg *A :
         Args.filtered(options::OPT_Wl_COMMA, options::OPT_Xlinker)) {
      for (StringRef V : A->getValues()) {
        StringRef Option = V.split('=').first;
        if (V == "-Bdynamic") {
          DynamicInputs = true;
          continue;
        }
        if (Option == "-shared" || Option == "--shared" || Option == "-pie" ||
            Option == "--pie" || Option == "-dy" ||
            Option == "-static" || Option == "--static" ||
            Option == "-Bstatic" || Option == "-dn" ||
            Option == "-no-pie" || Option == "--no-pie" ||
            Option == "--no-dynamic-linker" ||
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

    const bool Dynamic = Shared || PIE || DynamicInputs;
    if ((Shared && PIE) ||
        (Dynamic && (Relocatable || Args.hasArg(options::OPT_static))) ||
        (DynamicInputs && !Shared && !PIE &&
         !Args.hasArg(options::OPT_no_pie))) {
      D.Diag(diag::err_drv_unsupported_opt_for_target)
          << "conflicting MMIX Linux link modes" << TC.getTripleString();
      return;
    }

    const bool StartFiles =
        !Relocatable && !Shared &&
        !Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles);
    const bool DefaultLibs =
        !Relocatable &&
        !Args.hasArg(options::OPT_nostdlib, options::OPT_nodefaultlibs);
    const bool CXX = D.CCCIsCXX() || TC.ShouldLinkCXXStdlib(Args);
    const bool DynamicCXXCRT =
        Dynamic && CXX && !Relocatable &&
        !Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles);
    if (Dynamic && (DefaultLibs || StartFiles)) {
      const Arg *Unwind = Args.getLastArg(options::OPT_unwindlib_EQ);
      if (DefaultLibs &&
          (TC.ShouldLinkCXXStdlib(Args) ||
           (Unwind && StringRef(Unwind->getValue()) == "libunwind"))) {
        if (!TC.validateSharedCXXProfile())
          return;
      }
      if (!TC.validateSharedCProfile())
        return;
      if (!Shared)
        TC.getSysrootFile("ld-llvm-mmix.so.1", "lib");
    }
    ArgStringList CmdArgs{"-m", "elf64mmix_linux"};
    if (Relocatable)
      CmdArgs.push_back("-r");
    else if (Shared)
      CmdArgs.push_back("-shared");
    else if (PIE)
      CmdArgs.push_back("-pie");
    else if (!Dynamic)
      CmdArgs.push_back("-static");
    if (!Relocatable) {
      if (!Dynamic || Shared)
        CmdArgs.push_back("--no-dynamic-linker");
      else
        CmdArgs.push_back("--dynamic-linker=/lib/ld-llvm-mmix.so.1");
      if (Dynamic)
        CmdArgs.append({"-z", "now", "-z", "relro", "-z", "text"});
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
    Args.ClaimAllArgs(options::OPT_pie);
    Args.ClaimAllArgs(options::OPT_shared);
    Args.ClaimAllArgs(options::OPT_g_Group);
    Args.ClaimAllArgs(options::OPT_emit_llvm);
    Args.ClaimAllArgs(options::OPT_w);
    Args.addAllArgs(CmdArgs, {options::OPT_L, options::OPT_s, options::OPT_t,
                              options::OPT_u_Group, options::OPT_e});
    TC.AddFilePathLibArgs(Args, CmdArgs);
    if (StartFiles) {
      CmdArgs.push_back(Args.MakeArgString(
          TC.getSysrootFile(Dynamic ? "Scrt1.o" : "crt1.o")));
      if (!Dynamic)
        CmdArgs.push_back(
            TC.getCompilerRTArgString(Args, "crtbegin", ToolChain::FT_Object));
    }
    if (DynamicCXXCRT)
      CmdArgs.push_back(
          TC.getCompilerRTArgString(Args, "crtbegin", ToolChain::FT_Object));
    tools::addLinkerCompressDebugSectionsOption(TC, Args, CmdArgs);
    if (LTOMode == LTOK_Full || HasIRInput)
      tools::addLTOOptions(TC, Args, CmdArgs, Output, Inputs,
                           /*IsThinLTO=*/false);
    // Dynamic ET_EXEC also needs GOT-based references. Include archive-only
    // bitcode inputs, whose type is not visible to the Driver.
    if (Dynamic)
      CmdArgs.append({"-mllvm", "-relocation-model=pic"});
    tools::AddLinkerInputs(TC, Inputs, Args, CmdArgs, JA);
    if (DefaultLibs) {
      CmdArgs.push_back("--start-group");
      if (Dynamic) {
        if (TC.ShouldLinkCXXStdlib(Args))
          for (StringRef Name : {"libc++.so", "libc++abi.so", "libunwind.so"})
            CmdArgs.push_back(Args.MakeArgString(TC.getSysrootFile(Name)));
        // C-only profiles still need the forced-exit companions. A qualified
        // C++ profile supplies the full providers with the same SONAMEs.
        // Never mix static state owners into this composition.
        if (!Args.hasArg(options::OPT_nolibc)) {
          CmdArgs.push_back(Args.MakeArgString(TC.getSysrootFile("libc.so")));
          if (!TC.ShouldLinkCXXStdlib(Args))
            for (StringRef Name : {"libunwind.so.1", "libc++abi.so.1"})
              CmdArgs.push_back(Args.MakeArgString(TC.getSysrootFile(Name)));
        }
        CmdArgs.push_back("--no-allow-shlib-undefined");
        CmdArgs.push_back("--exclude-libs=libclang_rt.builtins.a");
      } else if (TC.ShouldLinkCXXStdlib(Args)) {
        TC.AddCXXStdlibLibArgs(Args, CmdArgs);
        CmdArgs.push_back(Args.MakeArgString(TC.getSysrootFile("libm.a")));
      } else if (TC.hasThreadedCXXProfile()) {
        // The complete libc's forced-exit frames use the C++ personality even
        // when the application itself contains only C code.
        CmdArgs.push_back(Args.MakeArgString(TC.getSysrootFile("libc++abi.a")));
      }
      if (!Dynamic && TC.GetUnwindLibType(Args) == ToolChain::UNW_CompilerRT)
        CmdArgs.push_back(Args.MakeArgString(TC.getSysrootFile("libunwind.a")));
      if (!Dynamic && !Args.hasArg(options::OPT_nolibc))
        CmdArgs.push_back(Args.MakeArgString(TC.getSysrootFile("libc.a")));
      CmdArgs.push_back(TC.getCompilerRTArgString(Args, "builtins"));
      CmdArgs.push_back("--end-group");
    }
    if ((StartFiles && !Dynamic) || DynamicCXXCRT)
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
  if (Args.hasFlag(options::OPT_femulated_tls, options::OPT_fno_emulated_tls,
                   false))
    Reject(Args.getLastArg(options::OPT_femulated_tls));
  if (const Arg *A = Args.getLastArg(options::OPT_unwindlib_EQ)) {
    A->claim();
    StringRef Value = A->getValue();
    if (Value != "none" && Value != "libunwind" && Value != "platform")
      Reject(A);
  }
  for (const Arg *A : Args.filtered(
           options::OPT_dynamic, options::OPT_rdynamic, options::OPT_static_pie,
           options::OPT_gcc_toolchain, options::OPT_gcc_install_dir_EQ,
           options::OPT_gcc_triple_EQ))
    Reject(A);

  if (const Arg *A = Args.getLastArg(options::OPT_pthread)) {
    if (D.SysRoot.empty()) {
      Reject(A);
      return;
    }
    const bool Dynamic = requestsDynamicLink(Args);
    SmallString<128> Profile(D.SysRoot);
    if (Dynamic) {
      if (!validateSharedCProfile())
        return;
      SharedC = true;
    } else {
      llvm::sys::path::append(Profile, "usr", "lib", "mmix-libc-profile");
      if (diagnoseMissing(*this, Profile, /*Directory=*/false))
        return;
      auto Contents = getVFS().getBufferForFile(Profile);
      StringRef Kind = Contents ? (*Contents)->getBuffer().trim() : StringRef();
      ThreadedCXX = Kind == "mmix-linux-static-pthread-cxx-v1";
      if (!ThreadedCXX && Kind != "mmix-linux-static-pthread-c-v1") {
        D.Diag(diag::err_drv_clang_unsupported)
            << "unrecognized MMIX Linux pthread libc profile";
        return;
      }
    }
    // A profile marker identifies the package, not arbitrary -L/-B inputs.
    // Validate headers for both modes. Keep the static package prerequisite;
    // dynamic link inputs are checked below according to suppression options.
    SmallString<128> Header(D.SysRoot);
    llvm::sys::path::append(Header, "usr", "include", "pthread.h");
    diagnoseMissing(*this, Header, /*Directory=*/false);
    if (!SharedC) {
      getSysrootFile("crt1.o");
      getSysrootFile("libc.a");
    }
    if (ThreadedCXX) {
      // The marker attests a coherently packaged runtime, not archive contents.
      // Require the whole composition even for C and compile-only consumers.
      for (StringRef Name : {"libc++.a", "libc++abi.a", "libunwind.a", "libm.a"})
        getSysrootFile(Name);
      SmallString<128> Config(D.SysRoot);
      llvm::sys::path::append(Config, "usr", "include", "c++", "v1");
      llvm::sys::path::append(Config, "__config_site");
      diagnoseMissing(*this, Config, /*Directory=*/false);
      getCompilerRT(Args, "builtins");
      getCompilerRT(Args, "crtbegin", FT_Object);
      getCompilerRT(Args, "crtend", FT_Object);
    }
    if (ThreadedCXX || SharedC) {
      for (auto Pair :
           {std::pair{options::OPT_fexceptions, options::OPT_fno_exceptions},
            std::pair{options::OPT_funwind_tables,
                      options::OPT_fno_unwind_tables}})
        if (!Args.hasFlag(Pair.first, Pair.second, true))
          Reject(Args.getLastArg(Pair.second));
      if (const Arg *A = Args.getLastArg(
              options::OPT_fcxx_exceptions, options::OPT_fno_cxx_exceptions,
              options::OPT_fexceptions, options::OPT_fno_exceptions))
        if (A->getOption().matches(options::OPT_fno_cxx_exceptions))
          Reject(A);
      if (const Arg *A = Args.getLastArg(options::OPT_fignore_exceptions))
        Reject(A);
      if (Args.hasFlag(options::OPT_fasynchronous_unwind_tables,
                       options::OPT_fno_asynchronous_unwind_tables, false))
        Reject(Args.getLastArg(options::OPT_fasynchronous_unwind_tables));
      if (const Arg *A = Args.getLastArg(options::OPT_unwindlib_EQ))
        if (StringRef(A->getValue()) == "none")
          Reject(A);
      if (const Arg *A = Args.getLastArg(
              options::OPT_fsjlj_exceptions, options::OPT_fseh_exceptions,
              options::OPT_fdwarf_exceptions, options::OPT_fwasm_exceptions))
        if (!A->getOption().matches(options::OPT_fdwarf_exceptions))
          Reject(A);
    }
  }
}

ToolChain::UnwindLibType
MMIXLinuxToolChain::GetUnwindLibType(const ArgList &Args) const {
  if (ThreadedCXX)
    return UNW_CompilerRT;
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

void MMIXLinuxToolChain::addClangTargetOptions(
    const ArgList &Args, ArgStringList &CC1Args, BoundArch,
    Action::OffloadKind) const {
  if (!Args.hasArg(options::OPT_ftlsmodel_EQ) &&
      std::get<0>(tools::ParsePICArgs(*this, Args)) == llvm::Reloc::Static)
    CC1Args.push_back("-ftls-model=local-exec");
  if (ThreadedCXX || SharedC) {
    // C callbacks may be traversed by forced C++ cleanup, including under LTO.
    // Unwind tables alone do not prevent inferred nounwind from deleting it.
    CC1Args.push_back("-fexceptions");
    CC1Args.push_back("-funwind-tables=1");
  }
}

bool MMIXLinuxToolChain::validateSharedCProfile() const {
  std::string Path = getSysrootFile("mmix-shared-libc-profile");
  if (Path.empty())
    return false;
  auto Contents = getVFS().getBufferForFile(Path);
  if (!Contents) {
    getDriver().Diag(diag::err_drv_no_such_file) << Path;
    return false;
  }
  if ((*Contents)->getBuffer().trim() != "mmix-linux-shared-pthread-c") {
    getDriver().Diag(diag::err_drv_clang_unsupported)
        << "unrecognized MMIX Linux shared C libc profile";
    return false;
  }
  return true;
}

bool MMIXLinuxToolChain::validateSharedCXXProfile() const {
  std::string Path = getSysrootFile("mmix-shared-cxx-profile");
  if (Path.empty())
    return false;
  auto Contents = getVFS().getBufferForFile(Path);
  if (!Contents || (*Contents)->getBuffer().trim() !=
                       "mmix-linux-shared-pthread-cxx") {
    getDriver().Diag(diag::err_drv_clang_unsupported)
        << "missing or incompatible MMIX Linux shared C++ profile";
    return false;
  }
  for (StringRef Name : {"libc++.so", "libc++abi.so", "libunwind.so"})
    getSysrootFile(Name);
  return !getDriver().getDiags().hasErrorOccurred();
}

std::string MMIXLinuxToolChain::getSysrootFile(StringRef Name,
                                               StringRef Directory) const {
  if (getDriver().SysRoot.empty()) {
    getDriver().Diag(diag::err_drv_clang_unsupported)
        << "implicit system resources without --sysroot for MMIX Linux";
    return {};
  }
  SmallString<128> Path(getDriver().SysRoot);
  llvm::sys::path::append(Path, Directory, Name);
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
