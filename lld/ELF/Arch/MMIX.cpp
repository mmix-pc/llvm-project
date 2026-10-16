//===- MMIX.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LinkerScript.h"
#include "OutputSections.h"
#include "RelocScan.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"
#include <atomic>
#include <cstring>
#include <optional>

using namespace llvm;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
constexpr uint32_t swymInstruction = 0xfd000000;
constexpr uint32_t setlOpcode = 0xe3;
constexpr uint32_t inchOpcode = 0xe4;
constexpr uint32_t incmhOpcode = 0xe5;
constexpr uint32_t incmlOpcode = 0xe6;
constexpr uint32_t goImmediateOpcode = 0x9f;
constexpr uint32_t pushgoImmediateOpcode = 0xbf;
constexpr uint32_t pc19ValueMask = 0xffff;
constexpr uint32_t pc27ValueMask = 0xffffff;
constexpr uint16_t shnMMIXRegister = SHN_LOPROC;
constexpr StringLiteral registerContentsSectionName = ".MMIX.reg_contents";
constexpr StringLiteral linkerAllocatedRegisterContentsSectionName =
    ".MMIX.reg_contents.linker_allocated";
constexpr uint16_t defaultFirstGlobalRegister = 255;
constexpr uint16_t minimumGlobalRegister = 32;
constexpr uint64_t linuxPageSize = 8192;

struct MMIXRelaxationSequence {
  uint8_t size;
  uint8_t opcodeMask;
  uint8_t opcode;
};

std::optional<MMIXRelaxationSequence> getRelaxationSequence(RelType type) {
  switch (type) {
  case R_MMIX_GETA:
    return MMIXRelaxationSequence{16, 0xfe, 0xf4};
  case R_MMIX_CBRANCH:
    return MMIXRelaxationSequence{24, 0xe0, 0x40};
  case R_MMIX_PUSHJ:
    return MMIXRelaxationSequence{20, 0xfe, 0xf2};
  case R_MMIX_JMP:
    return MMIXRelaxationSequence{20, 0xfe, 0xf0};
  default:
    return std::nullopt;
  }
}

bool isStubbableCall(RelType type) { return type == R_MMIX_PUSHJ_STUBBABLE; }

bool isDwarfPC64Field(const InputSectionBase &sec, RelType type) {
  // DWARF encodings are byte streams, not literals read by an octa load.
  // Partial links retain .eh_frame as a regular input section as well.
  return type == R_MMIX_PC_64 && !(sec.flags & SHF_EXECINSTR) &&
         (sec.name == ".eh_frame" || sec.name == ".gcc_except_table" ||
          sec.name.starts_with(".gcc_except_table."));
}

bool isTPRel(RelType type) {
  return type == R_MMIX_TPREL_LO16 || type == R_MMIX_TPREL_ML16 ||
         type == R_MMIX_TPREL_MH16 || type == R_MMIX_TPREL_HI16;
}

unsigned getTPRelShift(RelType type) {
  switch (type) {
  case R_MMIX_TPREL_LO16:
    return 0;
  case R_MMIX_TPREL_ML16:
    return 16;
  case R_MMIX_TPREL_MH16:
    return 32;
  case R_MMIX_TPREL_HI16:
    return 48;
  default:
    llvm_unreachable("not an MMIX TP-relative relocation");
  }
}

uint8_t getTPRelOpcode(RelType type) {
  switch (type) {
  case R_MMIX_TPREL_LO16:
    return setlOpcode;
  case R_MMIX_TPREL_ML16:
    return incmlOpcode;
  case R_MMIX_TPREL_MH16:
    return incmhOpcode;
  case R_MMIX_TPREL_HI16:
    return inchOpcode;
  default:
    llvm_unreachable("not an MMIX TP-relative relocation");
  }
}

class MMIXLinkerAllocatedRegisterSection final : public SyntheticSection {
public:
  MMIXLinkerAllocatedRegisterSection(Ctx &ctx, StringRef name)
      : SyntheticSection(ctx, name, SHT_PROGBITS, 0, 8) {}

  size_t getSize() const override { return bases.size() * 8; }
  bool isNeeded() const override { return needed.load(); }
  void markNeeded() { needed.store(true); }
  void writeTo(uint8_t *buf) override {
    for (auto [index, base] : llvm::enumerate(bases))
      write64be(buf + index * 8, base);
  }

  SmallVector<uint64_t, 0> bases;

private:
  std::atomic<bool> needed = false;
};

StringRef getUnsupportedRelocationReason(RelType type) {
  switch (type) {
  case R_MMIX_GETA_1:
  case R_MMIX_GETA_2:
  case R_MMIX_GETA_3:
  case R_MMIX_CBRANCH_J:
  case R_MMIX_CBRANCH_1:
  case R_MMIX_CBRANCH_2:
  case R_MMIX_CBRANCH_3:
  case R_MMIX_PUSHJ_1:
  case R_MMIX_PUSHJ_2:
  case R_MMIX_PUSHJ_3:
  case R_MMIX_JMP_1:
  case R_MMIX_JMP_2:
  case R_MMIX_JMP_3:
    return "GNU relaxation continuation cannot be used as standalone input";
  case R_MMIX_GNU_VTINHERIT:
  case R_MMIX_GNU_VTENTRY:
    return "requires GNU vtable metadata support";
  default:
    return {};
  }
}

bool isTerminalRelocation(RelType type) {
  return type == R_MMIX_ADDR19 || type == R_MMIX_ADDR27;
}

unsigned getRelocationFieldSize(RelType type) {
  switch (type) {
  case R_MMIX_8:
  case R_MMIX_PC_8:
  case R_MMIX_REG_OR_BYTE:
  case R_MMIX_REG:
    return 1;
  case R_MMIX_16:
  case R_MMIX_PC_16:
  case R_MMIX_BASE_PLUS_OFFSET:
    return 2;
  case R_MMIX_24:
  case R_MMIX_32:
  case R_MMIX_PC_24:
  case R_MMIX_PC_32:
  case R_MMIX_ADDR19:
  case R_MMIX_ADDR27:
    return 4;
  case R_MMIX_64:
  case R_MMIX_PC_64:
  case R_MMIX_GOT_PC64:
  case R_MMIX_GOTTPREL_PC64:
  case R_MMIX_TLSGD_PC64:
  case R_MMIX_TLSLD_PC64:
  case R_MMIX_DTPOFF64:
    return 8;
  default:
    return 0;
  }
}

void checkMMIXBitfield(Ctx &ctx, uint8_t *loc, uint64_t val, unsigned bits,
                       const Relocation &rel) {
  uint64_t mask = maxUIntN(bits);
  if (val > mask && val < ~mask)
    reportRangeError(ctx, loc, rel, Twine(static_cast<int64_t>(val)),
                     -static_cast<int64_t>(uint64_t(1) << bits), mask);
}

void relocateMMIXTerminal(uint8_t *loc, Ctx &ctx, uint64_t val,
                          uint32_t valueMask, const Relocation &rel) {
  constexpr int64_t instructionSize = 4;
  int64_t delta = static_cast<int64_t>(val);
  int64_t min = -static_cast<int64_t>(valueMask + 1) * instructionSize;
  int64_t max = static_cast<int64_t>(valueMask) * instructionSize;
  checkAlignment(ctx, loc, val, instructionSize, rel);
  if (delta < min || delta > max)
    reportRangeError(ctx, loc, rel, Twine(delta), min, max);

  constexpr uint32_t directionMask = uint32_t(1) << 24;
  uint32_t word = read32be(loc) & ~(directionMask | valueMask);
  if (delta < 0)
    word |= directionMask;
  word |= static_cast<uint64_t>(delta / instructionSize) & valueMask;
  write32be(loc, word);
}

bool isDirectMMIXTransfer(uint64_t target, uint64_t place, uint32_t valueMask) {
  if (target & 3)
    return false;
  int64_t delta = static_cast<int64_t>(target - place);
  constexpr int64_t instructionSize = 4;
  int64_t min = -static_cast<int64_t>(valueMask + 1) * instructionSize;
  int64_t max = static_cast<int64_t>(valueMask) * instructionSize;
  return delta >= min && delta <= max;
}

uint32_t encodeMMIXTerminal(uint32_t word, int64_t delta, uint32_t valueMask) {
  constexpr uint32_t directionMask = uint32_t(1) << 24;
  word &= ~(directionMask | valueMask);
  if (delta < 0)
    word |= directionMask;
  return word | (static_cast<uint64_t>(delta / 4) & valueMask);
}

void writeAbsoluteAddress(uint8_t *loc, uint8_t reg, uint64_t value) {
  write32be(loc, (setlOpcode << 24) | (uint32_t(reg) << 16) | (value & 0xffff));
  write32be(loc + 4, (incmlOpcode << 24) | (uint32_t(reg) << 16) |
                         ((value >> 16) & 0xffff));
  write32be(loc + 8, (incmhOpcode << 24) | (uint32_t(reg) << 16) |
                         ((value >> 32) & 0xffff));
  write32be(loc + 12, (inchOpcode << 24) | (uint32_t(reg) << 16) |
                          ((value >> 48) & 0xffff));
}

void relocateMMIXExpanded(uint8_t *loc, uint64_t value, const Relocation &rel) {
  constexpr uint8_t scratchRegister = 255;
  uint8_t originalX = loc[1];
  switch (rel.type) {
  case R_MMIX_GETA:
    writeAbsoluteAddress(loc, originalX, value);
    return;
  case R_MMIX_CBRANCH: {
    constexpr uint32_t conditionInversionBit = uint32_t(1) << 27;
    constexpr uint32_t predictionInversionBit = uint32_t(1) << 28;
    constexpr uint32_t branchFieldMask = 0xffff;
    constexpr uint32_t instructionsToSkip = 6;
    uint32_t branch = read32be(loc);
    branch ^= conditionInversionBit | predictionInversionBit;
    branch = (branch & ~branchFieldMask) | instructionsToSkip;
    write32be(loc, branch);
    writeAbsoluteAddress(loc + 4, scratchRegister, value);
    write32be(loc + 20, (goImmediateOpcode << 24) | 0xffff00);
    return;
  }
  case R_MMIX_PUSHJ:
    writeAbsoluteAddress(loc, scratchRegister, value);
    write32be(loc + 16, (pushgoImmediateOpcode << 24) |
                            (uint32_t(originalX) << 16) | 0xff00);
    return;
  case R_MMIX_JMP:
    writeAbsoluteAddress(loc, scratchRegister, value);
    write32be(loc + 16, (goImmediateOpcode << 24) | 0xffff00);
    return;
  default:
    llvm_unreachable("not an expanding MMIX relocation");
  }
}

class MMIX final : public TargetInfo {
public:
  MMIX(Ctx &ctx);

  void initTargetSpecificSections() override;
  std::optional<TargetSymbolTableEntry>
  getTargetSymbolTableEntry(const Symbol &sym) const override;
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  RelType getDynRel(RelType type) const override {
    return isLinux() && type == R_MMIX_64 ? type : RelType(0);
  }
  int64_t getImplicitAddend(const uint8_t *buf, RelType type) const override;
  template <class ELFT, class RelTy>
  void scanSectionImpl(InputSectionBase &sec, Relocs<RelTy> rels,
                       unsigned shard);
  void scanSection(InputSectionBase &sec, unsigned shard) override {
    elf::scanSection1<MMIX, ELF64BE>(*this, sec, shard);
  }
  bool relaxOnce(int pass) const override;
  void finalizeRelax(int passes) const override;
  void validateOutput(uint64_t fileSize) const override;
  void relocateAlloc(InputSection &sec, uint8_t *buf) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;

private:
  enum class RelaxationState : uint8_t {
    Pending,
    Direct,
    Expanded,
    Stub,
    Invalid
  };

  bool isLinux() const { return ctx.arg.emulation == "elf64mmix_linux"; }

  struct RelaxationSite {
    InputSection *section;
    uint32_t relocationIndex;
    RelaxationState state = RelaxationState::Pending;
    uint32_t stubIndex = UINT32_MAX;
    int64_t callDelta = 0;
  };

  struct CallStub {
    InputSection *section;
    Symbol *target;
    int64_t addend;
    uint64_t offset;
    uint8_t size;
    Defined *symbol;
  };

  struct RelaxationSection {
    InputSection *section;
    const uint8_t *originalContent;
    uint64_t originalSize;
    SmallVector<uint32_t, 0> stubIndices;
  };

  struct BasePlusOffsetRequest {
    InputSection *section;
    uint32_t relocationIndex;
    uint32_t baseIndex = UINT32_MAX;
    uint8_t offset = 0;
    bool valid = true;
  };

  mutable SmallVector<RelaxationSite, 0> relaxationSites;
  mutable DenseMap<const Relocation *, uint32_t> relaxationSiteByRelocation;
  mutable SmallVector<CallStub, 0> callStubs;
  mutable SmallVector<RelaxationSection, 0> relaxationSections;
  mutable SmallVector<BasePlusOffsetRequest, 0> basePlusOffsetRequests;
  mutable DenseMap<const Relocation *, uint32_t>
      basePlusOffsetRequestByRelocation;
  mutable bool relaxationSitesInitialized = false;
  mutable bool basePlusOffsetRequestsInitialized = false;
  mutable bool linkerAllocatedRegisterContentsOrdered = false;
  mutable uint16_t firstGlobalRegister = defaultFirstGlobalRegister;
  mutable OutputSection *registerContentsOutput = nullptr;

  std::unique_ptr<MMIXLinkerAllocatedRegisterSection>
      linkerAllocatedRegisterContents;

  DenseMap<const Symbol *, uint16_t> registerSymbols;
  DenseSet<const InputSection *> registerContentSections;

  void collectRegisterModel();
  void updateRegisterModel() const;
  bool orderLinkerAllocatedRegisterContents() const;
  bool planBasePlusOffsetAllocations() const;
  void validateLocalAssertions() const;
  const BasePlusOffsetRequest *
  findBasePlusOffsetRequest(const Relocation &rel) const;
  std::optional<uint64_t> addUnsignedAddend(uint64_t value, int64_t addend,
                                            const Relocation &rel,
                                            const uint8_t *loc,
                                            StringRef calculation) const;
  std::optional<uint16_t> resolveLocalAssertion(const Relocation &rel,
                                                const uint8_t *loc) const;
  bool isRegisterContentReference(const Symbol &sym) const;
  bool isRegisterContentSymbol(const Symbol &sym) const;
  void validateInputRelocations(InputSectionBase &sec) const;
  bool isDynamicOutput() const {
    return ctx.arg.shared || ctx.arg.pie || !ctx.arg.dynamicLinker.empty() ||
           !ctx.sharedFiles.empty();
  }
  void validateLinuxLayout() const;
  void validateDynamicRelocations() const;
  void validateSharedInput(SharedFile &file) const;
  std::optional<uint16_t>
  getRegisterContentValue(const Symbol &sym, int64_t addend,
                          const uint8_t *loc = nullptr) const;
  std::optional<uint8_t> resolveRegister(const Relocation &rel,
                                         bool allowImmediate,
                                         const uint8_t *loc) const;
  RelaxationSection &getRelaxationSection(InputSection &sec) const;
  uint32_t getOrCreateCallStub(RelaxationSite &site, uint64_t target,
                               uint64_t place) const;
  const RelaxationSite *findRelaxationSite(const Relocation &rel) const;
};
} // namespace

MMIX::MMIX(Ctx &ctx) : TargetInfo(ctx) {
  symbolicRel = R_MMIX_64;
  if (isLinux()) {
    // Linux fixes rG at process entry; the loader does not initialize GREGs.
    firstGlobalRegister = 230;
    defaultMaxPageSize = defaultCommonPageSize = linuxPageSize;
    relativeRel = R_MMIX_RELATIVE;
    gotRel = R_MMIX_GLOB_DAT;
    tlsGotRel = R_MMIX_TPOFF64;
    tlsModuleIndexRel = R_MMIX_DTPMOD64;
    tlsOffsetRel = R_MMIX_DTPOFF64;
  }
  if (ctx.arg.ekind != ELF64BEKind)
    ErrAlways(ctx) << "MMIX supports only ELF64 big-endian input and output";

  if (ctx.arg.shared && !isLinux())
    ErrAlways(ctx) << "MMIX does not support shared object output";
  else if (ctx.arg.pie && !isLinux())
    ErrAlways(ctx) << "MMIX does not support PIE output";
  else if (ctx.arg.relocatable && !ctx.bitcodeFiles.empty())
    ErrAlways(ctx) << "MMIX does not support relocatable links with bitcode "
                      "input";

  if (!ctx.arg.dynamicLinker.empty() && !isLinux())
    ErrAlways(ctx) << "MMIX does not support a dynamic linker";
  if (ctx.arg.oFormatBinary)
    ErrAlways(ctx) << "MMIX lld supports only ELF output";
  if (ctx.arg.osabi != ELFOSABI_NONE &&
      !(isLinux() && isDynamicOutput() && ctx.arg.osabi == ELFOSABI_LINUX))
    ErrAlways(ctx) << "MMIX supports only the System V ELF OSABI";
  if (!ctx.sharedFiles.empty() && !isLinux())
    ErrAlways(ctx) << "MMIX does not support dynamic shared object inputs";
  for (InputFile *file : ctx.objectFiles)
    if (file->abiVersion != 0)
      ErrAlways(ctx) << file << ": unsupported MMIX ELF ABI version "
                     << static_cast<unsigned>(file->abiVersion);

  if (isLinux())
    for (ELFFileBase *file : ctx.objectFiles) {
      if (file->getObj<ELF64BE>().getHeader().e_flags)
        ErrAlways(ctx) << file << ": MMIX Linux does not support ELF e_flags";
      // Stop before generic IFUNC processing can create resolver entries.
      for (const ELF64BE::Sym &sym : file->getELFSyms<ELF64BE>())
        if (sym.getType() == STT_GNU_IFUNC)
          Fatal(ctx) << file << ": MMIX Linux does not support IFUNC symbols";
        else if (isDynamicOutput() && sym.getBinding() == STB_GNU_UNIQUE)
          ErrAlways(ctx) << file
                         << ": MMIX Linux does not support GNU_UNIQUE symbols";
    }

  if (isLinux() && isDynamicOutput()) {
    // Relocation protection operates on whole Linux pages, including the
    // boundary between RELRO and ordinary mutable data.
    ctx.arg.zSeparate = SeparateSegmentKind::Loadable;
    // Dynamic images use eager GOT binding; neither a PLT nor a lazy resolver
    // is part of this ABI. Emit both hash tables for all supported consumers.
    ctx.arg.zNow = true;
    ctx.arg.sysvHash = ctx.arg.gnuHash = true;
    if (ctx.arg.shared) {
      // An explicit entry does not identify a process interpreter. Its
      // bootstrap restrictions belong to the runtime producer and consumer.
      if (!ctx.arg.warnMissingEntry)
        ctx.arg.entry = "";
      if (!ctx.arg.dynamicLinker.empty())
        ErrAlways(ctx) << "MMIX Linux shared objects do not support PT_INTERP";
    } else if (StringRef path = ctx.arg.dynamicLinker;
               !path.starts_with("/") || path == "/" ||
               path.contains("/../") || path.ends_with("/..")) {
      ErrAlways(ctx) << "MMIX Linux dynamic executables require an absolute "
                       "interpreter path without parent traversal";
    }
    if (!ctx.arg.isRela || ctx.arg.relrPackDynRelocs ||
        ctx.arg.androidPackDynRelocs)
      ErrAlways(ctx)
          << "MMIX Linux dynamic images require unpacked RELA relocations";
    if (!ctx.arg.zText)
      ErrAlways(ctx) << "MMIX Linux does not support text relocations";
    if (ctx.arg.bsymbolic != BsymbolicKind::None || ctx.arg.zGlobal ||
        ctx.arg.zInitfirst || ctx.arg.zInterpose || ctx.arg.zNodefaultlib ||
        ctx.arg.zNodlopen || ctx.arg.zOrigin || !ctx.arg.filterList.empty() ||
        !ctx.arg.auxiliaryList.empty())
      ErrAlways(ctx)
          << "MMIX Linux dynamic image requests unsupported dynamic semantics";
    if (ctx.arg.versionDefinitions.size() > 2)
      ErrAlways(ctx) << "MMIX Linux does not support symbol versions";
    for (SharedFile *file : ctx.sharedFiles)
      validateSharedInput(*file);
  }

  collectRegisterModel();
}

void MMIX::validateSharedInput(SharedFile &file) const {
  const auto &obj = file.getObj<ELF64BE>();
  const auto &header = obj.getHeader();
  if (header.e_ident[EI_ABIVERSION] || header.e_flags ||
      (header.e_ident[EI_OSABI] != ELFOSABI_NONE &&
       header.e_ident[EI_OSABI] != ELFOSABI_LINUX))
    ErrAlways(ctx) << &file << ": unsupported MMIX Linux shared object ABI";
  for (const ELF64BE::Sym &sym : file.getELFSyms<ELF64BE>()) {
    if (sym.getType() == STT_GNU_IFUNC)
      Fatal(ctx) << &file << ": MMIX Linux does not support IFUNC symbols";
    if (sym.getBinding() == STB_GNU_UNIQUE)
      ErrAlways(ctx) << &file
                     << ": MMIX Linux does not support GNU_UNIQUE symbols";
  }
  for (const ELF64BE::Shdr &shdr : file.getELFShdrs<ELF64BE>()) {
    StringRef name = check(obj.getSectionName(shdr));
    if (name == registerContentsSectionName ||
        name == linkerAllocatedRegisterContentsSectionName)
      ErrAlways(ctx) << &file
                     << ": MMIX Linux does not support loader-initialized "
                        "register contents";
    if (shdr.sh_type == SHT_GNU_versym || shdr.sh_type == SHT_GNU_verdef ||
        shdr.sh_type == SHT_GNU_verneed)
      ErrAlways(ctx) << &file
                     << ": MMIX Linux does not support symbol versions";
  }
  auto entries = obj.dynamicEntries();
  if (!entries) {
    ErrAlways(ctx) << &file << ": " << toString(entries.takeError());
    return;
  }
  for (const ELF64BE::Dyn &entry : *entries) {
    switch (entry.d_tag) {
    case DT_NULL:
      return;
    case DT_FLAGS:
      if (entry.d_un.d_val & ~uint64_t(DF_BIND_NOW | DF_STATIC_TLS))
        ErrAlways(ctx) << &file << ": unsupported MMIX Linux DT_FLAGS";
      break;
    case DT_FLAGS_1:
      if (entry.d_un.d_val & ~uint64_t(DF_1_NOW | DF_1_NODELETE))
        ErrAlways(ctx) << &file << ": unsupported MMIX Linux DT_FLAGS_1";
      break;
    case DT_STRTAB:
    case DT_STRSZ:
    case DT_SYMTAB:
    case DT_SYMENT:
    case DT_HASH:
    case DT_GNU_HASH:
    case DT_NEEDED:
    case DT_SONAME:
    case DT_RUNPATH:
    case DT_RPATH:
    case DT_RELA:
    case DT_RELASZ:
    case DT_RELAENT:
    case DT_RELACOUNT:
    case DT_INIT:
    case DT_FINI:
    case DT_INIT_ARRAY:
    case DT_INIT_ARRAYSZ:
    case DT_FINI_ARRAY:
    case DT_FINI_ARRAYSZ:
    case DT_BIND_NOW:
    case 0x6ffffdf8: // DT_CHECKSUM is nonsemantic metadata.
      break;
    default:
      ErrAlways(ctx) << &file << ": unsupported MMIX Linux dynamic tag "
                     << entry.d_tag;
    }
  }
  ErrAlways(ctx)
      << &file << ": MMIX Linux shared object has no terminated dynamic table";
}

void MMIX::initTargetSpecificSections() {
  if (isLinux() && !ctx.arg.relocatable &&
      (ctx.arg.maxPageSize < linuxPageSize ||
       ctx.arg.commonPageSize < linuxPageSize))
    Err(ctx) << "MMIX Linux requires maximum and common page sizes of at "
                "least 8192 bytes";
  StringRef name = ctx.script->hasSectionsCommand
                       ? linkerAllocatedRegisterContentsSectionName
                       : registerContentsSectionName;
  linkerAllocatedRegisterContents =
      std::make_unique<MMIXLinkerAllocatedRegisterSection>(ctx, name);
  ctx.inputSections.push_back(linkerAllocatedRegisterContents.get());

  // Inspect selected Linux inputs before GC can hide incompatible relocations.
  if (ctx.arg.relocatable || isLinux())
    for (ELFFileBase *file : ctx.objectFiles)
      for (InputSectionBase *section : file->getSections())
        if (section && section != &InputSection::discarded &&
            section->kind() == SectionBase::Regular)
          validateInputRelocations(*section);
}

void MMIX::validateInputRelocations(InputSectionBase &sec) const {
  RelsOrRelas<ELF64BE> relocs = sec.relsOrRelas<ELF64BE>();
  if (relocs.areRelocsRel() || relocs.areRelocsCrel()) {
    Err(ctx) << &sec << ": MMIX supports only RELA relocations";
    return;
  }

  ArrayRef<uint8_t> contents = sec.content();
  for (const ELF64BE::Rela &rela : relocs.relas) {
    RelType type = rela.getType(false);
    if (type == R_MMIX_NONE)
      continue;
    // Final TLS admission belongs to the live relocation scan, after GC.
    if (isTPRel(type) && !ctx.arg.relocatable)
      continue;
    Symbol &sym = sec.getFile<ELF64BE>()->getSymbol(rela.getSymbol(false));
    const uint8_t *loc = contents.data();
    if (rela.r_offset < contents.size())
      loc += rela.r_offset;
    (void)getRelExpr(type, sym, loc);
    if (isLinux() &&
        (type == R_MMIX_GOT_PC64 || type == R_MMIX_GOTTPREL_PC64 ||
         type == R_MMIX_TLSGD_PC64 || type == R_MMIX_TLSLD_PC64 ||
         type == R_MMIX_DTPOFF64 ||
         (type == R_MMIX_PC_64 && (sec.flags & SHF_ALLOC)))) {
      uint64_t offset = rela.r_offset;
      if (!(sec.flags & SHF_ALLOC) || sec.type == SHT_NOBITS ||
          offset > contents.size() || contents.size() - offset < 8 ||
          (!isDwarfPC64Field(sec, type) &&
           ((offset & 7) || sec.addralign < 8)))
        Err(ctx) << &sec << ": relocation " << type << " at offset " << offset
                 << " requires a complete aligned 8-byte literal in an "
                    "allocated file-backed section";
    }
  }
}

bool MMIX::orderLinkerAllocatedRegisterContents() const {
  if (linkerAllocatedRegisterContentsOrdered ||
      ctx.script->hasSectionsCommand ||
      !linkerAllocatedRegisterContents->isNeeded())
    return false;
  linkerAllocatedRegisterContentsOrdered = true;

  OutputSection *output = linkerAllocatedRegisterContents->getParent();
  if (!output)
    return false;
  for (SectionCommand *cmd : output->commands) {
    auto *isd = dyn_cast<InputSectionDescription>(cmd);
    if (!isd)
      continue;
    auto it = llvm::find(isd->sections, linkerAllocatedRegisterContents.get());
    if (it == isd->sections.end())
      continue;
    if (it == isd->sections.begin())
      return false;
    InputSection *section = *it;
    isd->sections.erase(it);
    isd->sections.insert(isd->sections.begin(), section);
    return true;
  }
  return false;
}

void MMIX::collectRegisterModel() {
  for (ELFFileBase *file : ctx.objectFiles) {
    ArrayRef<ELF64BE::Sym> elfSymbols = file->getELFSyms<ELF64BE>();
    ArrayRef<Symbol *> symbols = file->getSymbols();
    ArrayRef<InputSectionBase *> sections = file->getSections();

    for (InputSectionBase *section : sections) {
      if (isLinux() && section && section != &InputSection::discarded &&
          (section->name == registerContentsSectionName ||
           section->name == linkerAllocatedRegisterContentsSectionName)) {
        Err(ctx) << section
                 << ": MMIX Linux does not support loader-initialized "
                    "register contents";
        continue;
      }
      if (!section || section == &InputSection::discarded ||
          section->name != registerContentsSectionName)
        continue;
      InputSection *input = dyn_cast<InputSection>(section);
      if (!input) {
        Err(ctx) << section
                 << ": MMIX register contents must be a regular "
                    "input section";
        continue;
      }
      registerContentSections.insert(input);
      if (input->type != SHT_PROGBITS)
        Err(ctx) << input << ": MMIX register contents must use SHT_PROGBITS";
      if (input->flags & SHF_ALLOC)
        Err(ctx) << input << ": MMIX register contents must not be allocated";
      // GNU as emits this target-defined section with sh_addralign 1. Its
      // entries are nevertheless octabytes, so promote the input alignment
      // before generic output-section layout combines the contents.
      input->addralign = std::max(input->addralign, uint32_t(8));
      if (input->getSize() % 8 != 0)
        Err(ctx) << input
                 << ": MMIX register contents size is not a multiple of 8";
    }

    for (auto [index, elfSym] : llvm::enumerate(elfSymbols)) {
      Symbol *sym = symbols[index];
      uint32_t sectionIndex = elfSym.st_shndx;
      if (sectionIndex == shnMMIXRegister) {
        bool valid = true;
        if (elfSym.getType() != STT_NOTYPE) {
          Err(ctx) << file << ": MMIX register symbol " << sym
                   << " must use STT_NOTYPE";
          valid = false;
        }
        if (elfSym.st_size != 0) {
          Err(ctx) << file << ": MMIX register symbol " << sym
                   << " must have size zero";
          valid = false;
        }
        if (elfSym.st_value < minimumGlobalRegister || elfSym.st_value > 255) {
          Err(ctx) << file << ": MMIX register symbol " << sym
                   << " has invalid register number " << elfSym.st_value;
          valid = false;
        }
        if (valid && sym->isDefined() && sym->file == file)
          registerSymbols.try_emplace(sym, elfSym.st_value);
        continue;
      }

      if (sectionIndex >= sections.size())
        continue;
      InputSectionBase *section = sections[sectionIndex];
      if (!section || section == &InputSection::discarded ||
          section->name != registerContentsSectionName ||
          elfSym.getType() == STT_SECTION)
        continue;
      if (elfSym.getType() != STT_NOTYPE)
        Err(ctx) << file << ": MMIX register-content symbol " << sym
                 << " must use STT_NOTYPE";
      if (elfSym.st_size != 0)
        Err(ctx) << file << ": MMIX register-content symbol " << sym
                 << " must have size zero";
      if ((elfSym.st_value & 7) != 0)
        Err(ctx) << file << ": MMIX register-content symbol " << sym
                 << " is not 8-byte aligned";
      if (elfSym.st_value >= section->getSize())
        Err(ctx) << file << ": MMIX register-content symbol " << sym
                 << " is outside its content section";
    }
  }
}

bool MMIX::isRegisterContentSymbol(const Symbol &sym) const {
  return isRegisterContentReference(sym) && sym.type != STT_SECTION;
}

bool MMIX::isRegisterContentReference(const Symbol &sym) const {
  const Defined *defined = dyn_cast<Defined>(&sym);
  const InputSection *section =
      defined ? dyn_cast_or_null<InputSection>(defined->section) : nullptr;
  return section && registerContentSections.contains(section);
}

std::optional<TargetSymbolTableEntry>
MMIX::getTargetSymbolTableEntry(const Symbol &sym) const {
  auto direct = registerSymbols.find(&sym);
  if (direct != registerSymbols.end())
    return TargetSymbolTableEntry{shnMMIXRegister, direct->second};
  if (ctx.arg.relocatable)
    return std::nullopt;
  if (!isRegisterContentSymbol(sym))
    return std::nullopt;
  std::optional<uint16_t> reg = getRegisterContentValue(sym, 0);
  return TargetSymbolTableEntry{shnMMIXRegister, reg.value_or(0)};
}

void MMIX::updateRegisterModel() const {
  bool hasAllocatedContents = linkerAllocatedRegisterContents &&
                              linkerAllocatedRegisterContents->isNeeded();
  if (registerContentSections.empty() && !hasAllocatedContents)
    return;

  OutputSection *output = nullptr;
  if (hasAllocatedContents) {
    output = linkerAllocatedRegisterContents->getParent();
    if (!output || output->name != registerContentsSectionName) {
      Err(ctx) << linkerAllocatedRegisterContents.get()
               << ": MMIX linker-allocated register contents must be placed "
                  "in the .MMIX.reg_contents output section";
      output = nullptr;
    } else if (linkerAllocatedRegisterContents->outSecOff != 0) {
      Err(ctx) << linkerAllocatedRegisterContents.get()
               << ": MMIX linker-allocated register contents must precede "
                  "ordinary .MMIX.reg_contents input sections";
    }
  }

  for (const InputSection *section : registerContentSections) {
    if (!section->isLive())
      continue;
    OutputSection *parent = section->getParent();
    if (!parent || parent->name != registerContentsSectionName) {
      Err(ctx) << section
               << ": MMIX register contents must be placed in the "
                  ".MMIX.reg_contents output section";
      continue;
    }
    if (output && output != parent) {
      Err(ctx) << section
               << ": MMIX register contents use multiple output sections";
      continue;
    }
    output = parent;
    if ((section->outSecOff & 7) != 0)
      Err(ctx) << section
               << ": MMIX register contents have an unaligned output offset";
    if (hasAllocatedContents &&
        section->outSecOff < linkerAllocatedRegisterContents->getSize())
      Err(ctx) << section
               << ": ordinary MMIX register contents must follow "
                  ".MMIX.reg_contents.linker_allocated";
  }
  if (!output)
    return;
  registerContentsOutput = output;
  if ((output->size & 7) != 0) {
    Err(ctx) << "output section " << output->name
             << ": MMIX register contents output size is not a multiple of 8";
    return;
  }

  uint64_t entries = output->size / 8;
  constexpr uint64_t maximumEntries =
      defaultFirstGlobalRegister - minimumGlobalRegister;
  if (entries > maximumEntries) {
    Err(ctx) << "output section " << output->name
             << ": too many MMIX global register contents: " << entries
             << ", maximum is " << maximumEntries;
    entries = maximumEntries;
  }
  firstGlobalRegister = defaultFirstGlobalRegister - entries;
}

bool MMIX::planBasePlusOffsetAllocations() const {
  if (!basePlusOffsetRequestsInitialized) {
    SmallVector<InputSection *, 0> storage;
    for (OutputSection *osec : ctx.outputSections) {
      for (InputSection *sec : getInputSections(*osec, storage)) {
        ArrayRef<Relocation> rels = sec->relocs();
        for (auto [index, rel] : llvm::enumerate(rels)) {
          if (rel.type != R_MMIX_BASE_PLUS_OFFSET)
            continue;
          uint32_t requestIndex = basePlusOffsetRequests.size();
          basePlusOffsetRequests.push_back({sec, static_cast<uint32_t>(index)});
          basePlusOffsetRequestByRelocation.try_emplace(&rels[index],
                                                        requestIndex);
        }
      }
    }
    basePlusOffsetRequestsInitialized = true;
  }

  struct ResolvedRequest {
    uint64_t value;
    uint32_t requestIndex;
  };
  SmallVector<ResolvedRequest, 0> resolved;
  for (auto [index, request] : llvm::enumerate(basePlusOffsetRequests)) {
    if (!request.valid)
      continue;
    Relocation &rel = request.section->relocs()[request.relocationIndex];
    if (registerSymbols.contains(rel.sym) ||
        isRegisterContentReference(*rel.sym)) {
      const uint8_t *loc = request.section->content().data() + rel.offset;
      Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << rel.type
               << " cannot use register symbol " << rel.sym << " as an address";
      request.valid = false;
      rel.expr = R_NONE;
      continue;
    }
    const uint8_t *loc = request.section->content().data() + rel.offset;
    std::optional<uint64_t> value = addUnsignedAddend(
        rel.sym->getVA(ctx), rel.addend, rel, loc, "address calculation");
    if (!value) {
      request.valid = false;
      rel.expr = R_NONE;
      continue;
    }
    resolved.push_back({*value, static_cast<uint32_t>(index)});
  }
  llvm::sort(resolved, [](const ResolvedRequest &a, const ResolvedRequest &b) {
    return a.value < b.value;
  });

  SmallVector<uint64_t, 0> bases;
  uint64_t currentBase = 0;
  for (auto [index, item] : llvm::enumerate(resolved)) {
    if (index == 0 || item.value - currentBase > 255) {
      currentBase = item.value;
      bases.push_back(currentBase);
    }
    BasePlusOffsetRequest &request = basePlusOffsetRequests[item.requestIndex];
    request.baseIndex = bases.size() - 1;
    request.offset = item.value - currentBase;
  }

  bool changed = linkerAllocatedRegisterContents->bases != bases;
  linkerAllocatedRegisterContents->bases = std::move(bases);
  return changed;
}

std::optional<uint64_t> MMIX::addUnsignedAddend(uint64_t value, int64_t addend,
                                                const Relocation &rel,
                                                const uint8_t *loc,
                                                StringRef calculation) const {
  if (addend >= 0) {
    uint64_t unsignedAddend = static_cast<uint64_t>(addend);
    if (value <= UINT64_MAX - unsignedAddend)
      return value + unsignedAddend;
  } else {
    uint64_t magnitude = uint64_t(-(addend + 1)) + 1;
    if (value >= magnitude)
      return value - magnitude;
  }
  Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << rel.type << ' '
           << calculation << " overflows the 64-bit address range";
  return std::nullopt;
}

std::optional<uint16_t> MMIX::resolveLocalAssertion(const Relocation &rel,
                                                    const uint8_t *loc) const {
  auto direct = registerSymbols.find(rel.sym);
  if (direct != registerSymbols.end()) {
    std::optional<uint64_t> value = addUnsignedAddend(
        direct->second, rel.addend, rel, loc, "register calculation");
    if (!value)
      return std::nullopt;
    if (*value <= 255)
      return static_cast<uint16_t>(*value);
  } else if (isRegisterContentReference(*rel.sym)) {
    return getRegisterContentValue(*rel.sym, rel.addend, loc);
  } else if (rel.sym->isUndefined() && rel.sym->getName().empty()) {
    std::optional<uint64_t> value = addUnsignedAddend(
        0, rel.addend, rel, loc, "register calculation");
    if (!value)
      return std::nullopt;
    if (*value <= 255)
      return static_cast<uint16_t>(*value);
  } else if (const Defined *defined = dyn_cast<Defined>(rel.sym);
             defined && !defined->section) {
    std::optional<uint64_t> value = addUnsignedAddend(
        rel.sym->getVA(ctx), rel.addend, rel, loc, "register calculation");
    if (!value)
      return std::nullopt;
    if (*value <= 255)
      return static_cast<uint16_t>(*value);
  } else {
    Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << rel.type
             << " requires a register or absolute value, but " << rel.sym
             << " is neither";
    return std::nullopt;
  }

  Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << rel.type
           << " resolves outside the register range [0, 255]";
  return std::nullopt;
}

void MMIX::validateLocalAssertions() const {
  SmallVector<InputSection *, 0> storage;
  for (OutputSection *osec : ctx.outputSections) {
    for (InputSection *sec : getInputSections(*osec, storage)) {
      for (Relocation &rel : sec->relocs()) {
        if (rel.type != R_MMIX_LOCAL)
          continue;
        const uint8_t *loc = sec->content().data() + rel.offset;
        std::optional<uint16_t> reg = resolveLocalAssertion(rel, loc);
        if (!reg)
          continue;
        if (*reg >= firstGlobalRegister)
          Err(ctx) << getErrorLoc(ctx, loc) << "R_MMIX_LOCAL register $" << *reg
                   << " is not local; first global register is $"
                   << firstGlobalRegister;
      }
    }
  }
}

const MMIX::BasePlusOffsetRequest *
MMIX::findBasePlusOffsetRequest(const Relocation &rel) const {
  auto it = basePlusOffsetRequestByRelocation.find(&rel);
  return it == basePlusOffsetRequestByRelocation.end()
             ? nullptr
             : &basePlusOffsetRequests[it->second];
}

std::optional<uint16_t>
MMIX::getRegisterContentValue(const Symbol &sym, int64_t addend,
                              const uint8_t *loc) const {
  const Defined &defined = cast<Defined>(sym);
  const InputSection &section = *cast<InputSection>(defined.section);
  uint64_t baseOffset = section.getOffset(defined.value);
  OutputSection *output = section.getParent();
  auto report = [&](const Twine &message) {
    if (loc)
      Err(ctx) << getErrorLoc(ctx, loc) << message;
    else
      Err(ctx) << message;
  };
  std::optional<uint64_t> offset;
  if (addend >= 0) {
    uint64_t unsignedAddend = static_cast<uint64_t>(addend);
    if (baseOffset <= UINT64_MAX - unsignedAddend)
      offset = baseOffset + unsignedAddend;
  } else {
    uint64_t magnitude = uint64_t(-(addend + 1)) + 1;
    if (baseOffset >= magnitude)
      offset = baseOffset - magnitude;
  }
  if (!offset || *offset >= output->size) {
    report(Twine("register-content symbol ") + sym.getName() +
           " with addend resolves outside .MMIX.reg_contents");
    return std::nullopt;
  }
  if ((*offset & 7) != 0) {
    report(Twine("register-content symbol ") + sym.getName() +
           " with addend is not 8-byte aligned");
    return std::nullopt;
  }
  return firstGlobalRegister + *offset / 8;
}

std::optional<uint8_t> MMIX::resolveRegister(const Relocation &rel,
                                             bool allowImmediate,
                                             const uint8_t *loc) const {
  auto direct = registerSymbols.find(rel.sym);
  std::optional<uint64_t> value;
  if (direct != registerSymbols.end()) {
    value = addUnsignedAddend(direct->second, rel.addend, rel, loc,
                              "register calculation");
  } else if (isRegisterContentReference(*rel.sym)) {
    std::optional<uint16_t> reg =
        getRegisterContentValue(*rel.sym, rel.addend, loc);
    if (!reg)
      return std::nullopt;
    value = *reg;
  } else if (allowImmediate) {
    const Defined *defined = dyn_cast<Defined>(rel.sym);
    if (!defined || defined->section) {
      Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << rel.type
               << " requires a register symbol or absolute byte, but "
               << rel.sym << " is neither";
      return std::nullopt;
    }
    value = addUnsignedAddend(rel.sym->getVA(ctx), rel.addend, rel, loc,
                              "immediate calculation");
  } else {
    Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << rel.type
             << " requires a register symbol, but " << rel.sym << " is not one";
    return std::nullopt;
  }

  if (!value)
    return std::nullopt;
  if (*value > 255) {
    Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << rel.type
             << " resolves to " << *value << ", outside byte range [0, 255]";
    return std::nullopt;
  }
  return static_cast<uint8_t>(*value);
}

RelExpr MMIX::getRelExpr(RelType type, const Symbol &s,
                         const uint8_t *loc) const {
  if (isLinux()) {
    switch (type) {
    case R_MMIX_RELATIVE:
    case R_MMIX_GLOB_DAT:
    case R_MMIX_DTPMOD64:
    case R_MMIX_TPOFF64:
      Err(ctx) << getErrorLoc(ctx, loc) << "runtime relocation " << type
               << " cannot be used in relocatable input";
      return R_NONE;
    case R_MMIX_DTPOFF64:
    case R_MMIX_TLSGD_PC64:
    case R_MMIX_TLSLD_PC64:
    case R_MMIX_GOTTPREL_PC64:
      if (!s.isTls()) {
        Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << type
                 << " requires an STT_TLS symbol: " << &s;
        return R_NONE;
      }
      if (!ctx.arg.relocatable) {
        if (s.isUndefined()) {
          Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << type
                   << " requires a defined TLS provider: " << &s;
          return R_NONE;
        }
        if (const auto *d = dyn_cast<Defined>(&s)) {
          if (!d->section || d->section == &InputSection::discarded ||
              (d->section->flags & (SHF_ALLOC | SHF_TLS)) !=
                  (SHF_ALLOC | SHF_TLS)) {
            Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << type
                     << " requires a definition in an allocated TLS section: "
                     << &s;
            return R_NONE;
          }
        }
        if ((type == R_MMIX_TLSLD_PC64 || type == R_MMIX_DTPOFF64) &&
            (!s.isDefined() || s.isPreemptible)) {
          Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << type
                   << " requires a nonpreemptible local TLS definition: " << &s;
          return R_NONE;
        }
      }
      if (type == R_MMIX_DTPOFF64)
        return R_DTPREL;
      if (type == R_MMIX_TLSGD_PC64)
        return R_TLSGD_PC;
      if (type == R_MMIX_TLSLD_PC64)
        return R_TLSLD_PC;
      return R_GOT_PC;
    default:
      break;
    }
  }
  if (type == R_MMIX_GOT_PC64) {
    if (!isLinux()) {
      Err(ctx) << getErrorLoc(ctx, loc)
               << "R_MMIX_GOT_PC64 requires MMIX Linux output";
      return R_NONE;
    }
    if (s.type != STT_NOTYPE && !s.isObject() && !s.isFunc()) {
      Err(ctx) << getErrorLoc(ctx, loc)
               << "R_MMIX_GOT_PC64 requires an ordinary address symbol: " << &s;
      return R_NONE;
    }
    if (registerSymbols.contains(&s) || isRegisterContentReference(s)) {
      Err(ctx) << getErrorLoc(ctx, loc)
               << "R_MMIX_GOT_PC64 cannot reference a register symbol: " << &s;
      return R_NONE;
    }
    return R_GOT_PC;
  }
  if (s.isPreemptible &&
      (isTerminalRelocation(type) || getRelaxationSequence(type) ||
       isStubbableCall(type) || type == R_MMIX_PC_64)) {
    Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << type
             << " cannot bind directly to preemptible symbol " << &s
             << "; use a GOT-relative address";
    return R_NONE;
  }
  if (isTPRel(type)) {
    if (!isLinux() || ctx.arg.shared) {
      Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << type << " against "
               << &s << " requires MMIX Linux executable output";
      return R_NONE;
    }
    if (!s.isTls()) {
      Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << type << " against "
               << &s << " requires an STT_TLS symbol";
      return R_NONE;
    }
    if (!ctx.arg.relocatable) {
      const auto *d = dyn_cast<Defined>(&s);
      if (!d || s.isPreemptible || !d->section ||
          d->section == &InputSection::discarded ||
          !d->section->isLive() ||
          (d->section->flags & (SHF_ALLOC | SHF_TLS)) !=
              (SHF_ALLOC | SHF_TLS)) {
        Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << type
                 << " against " << &s
                 << " requires a defined symbol in the executable TLS image";
        return R_NONE;
      }
    }
    return R_TPREL;
  }
  if (isLinux() && type == R_MMIX_BASE_PLUS_OFFSET) {
    Err(ctx) << getErrorLoc(ctx, loc)
             << "MMIX Linux does not support R_MMIX_BASE_PLUS_OFFSET "
                "requiring loader-initialized global registers";
    return R_NONE;
  }
  RelExpr expr = R_NONE;
  bool implemented = true;
  switch (type) {
  case R_MMIX_NONE:
    return R_NONE;
  case R_MMIX_8:
  case R_MMIX_16:
  case R_MMIX_24:
  case R_MMIX_32:
  case R_MMIX_64:
  case R_MMIX_REG_OR_BYTE:
  case R_MMIX_REG:
  case R_MMIX_BASE_PLUS_OFFSET:
  case R_MMIX_LOCAL:
    expr = R_ABS;
    break;
  case R_MMIX_PC_8:
  case R_MMIX_PC_16:
  case R_MMIX_PC_24:
  case R_MMIX_PC_32:
  case R_MMIX_PC_64:
  case R_MMIX_ADDR19:
  case R_MMIX_ADDR27:
  case R_MMIX_GETA:
  case R_MMIX_CBRANCH:
  case R_MMIX_PUSHJ:
  case R_MMIX_JMP:
  case R_MMIX_PUSHJ_STUBBABLE:
    expr = R_PC;
    break;
  default:
    implemented = false;
    break;
  }

  if (!implemented) {
    StringRef reason = getUnsupportedRelocationReason(type);
    if (reason.empty())
      Err(ctx) << getErrorLoc(ctx, loc) << "unknown relocation (" << type.v
               << ") against symbol " << &s;
    else
      Err(ctx) << getErrorLoc(ctx, loc) << "unsupported relocation " << type
               << " against symbol " << &s << ": " << reason;
    return R_NONE;
  }

  if (s.isTls()) {
    Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << type
             << " against TLS symbol " << &s << " is unsupported";
    return R_NONE;
  }
  return expr;
}

int64_t MMIX::getImplicitAddend(const uint8_t *buf, RelType type) const {
  Err(ctx) << getErrorLoc(ctx, buf) << "MMIX supports only RELA relocations; "
           << type << " has no explicit addend";
  return 0;
}

template <class ELFT, class RelTy>
void MMIX::scanSectionImpl(InputSectionBase &sec, Relocs<RelTy> rels,
                           unsigned shard) {
  RelocScan rs(ctx, &sec, shard);
  sec.relocations.reserve(rels.size());

  for (auto it = rels.begin(); it != rels.end(); ++it) {
    RelType type = it->getType(false);
    if (type == R_MMIX_NONE)
      continue;

    if (isLinux() &&
        (type == R_MMIX_GOTTPREL_PC64 || type == R_MMIX_TLSGD_PC64 ||
         type == R_MMIX_TLSLD_PC64 || type == R_MMIX_DTPOFF64)) {
      uint64_t offset = it->r_offset;
      Symbol &sym = sec.getFile<ELFT>()->getSymbol(it->getSymbol(false));
      if (offset > sec.content().size() || sec.content().size() - offset < 8)
        continue; // Input validation already diagnosed an incomplete literal.
      RelExpr expr = getRelExpr(type, sym, sec.content().data() + offset);
      if (expr == R_NONE)
        continue;
      int64_t addend = rs.getAddend<ELFT>(*it, type);
      if (type == R_MMIX_GOTTPREL_PC64)
        rs.handleTlsIe<false>(expr, type, offset, addend, sym);
      else if (type == R_MMIX_TLSGD_PC64)
        rs.handleTlsGd(expr, R_NONE, R_NONE, type, offset, addend, sym);
      else {
        if (type == R_MMIX_TLSLD_PC64)
          ctx.needsTlsLd.store(true, std::memory_order_relaxed);
        sec.addReloc({expr, type, offset, addend, &sym});
      }
      continue;
    }

    if (isTPRel(type)) {
      const uint64_t offset = it->r_offset;
      Symbol &sym = sec.getFile<ELFT>()->getSymbol(it->getSymbol(false));
      ArrayRef<uint8_t> contents = sec.content();
      if (!(sec.flags & SHF_EXECINSTR) || !(sec.flags & SHF_ALLOC) ||
          offset >= contents.size() || 4 > contents.size() - offset ||
          (offset & 3) || contents[offset] != getTPRelOpcode(type)) {
        Err(ctx) << &sec << ": relocation " << type << " against " << &sym
                 << " at offset " << offset
                 << " requires a complete aligned matching instruction in an "
                    "allocated executable section";
        continue;
      }
    }

    if (type == R_MMIX_BASE_PLUS_OFFSET && !isLinux())
      linkerAllocatedRegisterContents->markNeeded();

    if (type == R_MMIX_LOCAL && it->r_offset >= sec.getSize()) {
      Symbol &sym = sec.getFile<ELFT>()->getSymbol(it->getSymbol(false));
      Err(ctx) << &sec << ": R_MMIX_LOCAL metadata offset " << it->r_offset
               << " is outside the section against symbol " << &sym;
      continue;
    }

    if ((getRelaxationSequence(type) || isStubbableCall(type)) &&
        !(sec.flags & SHF_EXECINSTR)) {
      Symbol &sym = sec.getFile<ELFT>()->getSymbol(it->getSymbol(false));
      Err(ctx) << &sec << ": relaxation relocation " << type
               << " is not in an executable section against symbol " << &sym;
      continue;
    }

    if (std::optional<MMIXRelaxationSequence> sequence =
            getRelaxationSequence(type)) {
      uint64_t offset = it->r_offset;
      Symbol &sym = sec.getFile<ELFT>()->getSymbol(it->getSymbol(false));
      if ((offset & 3) != 0) {
        Err(ctx) << &sec << ": relaxation relocation " << type << " offset "
                 << offset << " is not 4-byte aligned against symbol " << &sym;
        continue;
      }
      ArrayRef<uint8_t> contents = sec.content();
      if (offset >= contents.size() ||
          sequence->size > contents.size() - offset) {
        Err(ctx) << &sec << ": " << type << " requires a "
                 << static_cast<unsigned>(sequence->size)
                 << "-byte reserved sequence at offset " << offset
                 << " against symbol " << &sym;
        continue;
      }

      contents = contents.slice(offset, sequence->size);
      if ((contents[0] & sequence->opcodeMask) != sequence->opcode) {
        Err(ctx) << &sec << ": " << type
                 << " reserved sequence has an invalid primary instruction "
                 << "at offset " << offset << " against symbol " << &sym;
        continue;
      }

      bool validPadding = true;
      for (unsigned i = 4; i < sequence->size; i += 4)
        validPadding &= read32be(contents.data() + i) == swymInstruction;
      if (!validPadding) {
        Err(ctx) << &sec << ": " << type
                 << " reserved sequence contains non-SWYM padding at offset "
                 << offset << " against symbol " << &sym;
        continue;
      }
    }

    if (isStubbableCall(type)) {
      uint64_t offset = it->r_offset;
      Symbol &sym = sec.getFile<ELFT>()->getSymbol(it->getSymbol(false));
      ArrayRef<uint8_t> contents = sec.content();
      if ((offset & 3) != 0) {
        Err(ctx) << &sec << ": stubbable call relocation offset " << offset
                 << " is not 4-byte aligned against symbol " << &sym;
        continue;
      }
      if (offset >= contents.size() || 4 > contents.size() - offset) {
        Err(ctx) << &sec << ": " << type
                 << " requires a 4-byte PUSHJ instruction at offset " << offset
                 << " against symbol " << &sym;
        continue;
      }
      if ((contents[offset] & 0xfe) != 0xf2) {
        Err(ctx) << &sec << ": " << type
                 << " does not reference a PUSHJ instruction at offset "
                 << offset << " against symbol " << &sym;
        continue;
      }
    }

    if (unsigned size = getRelocationFieldSize(type)) {
      uint64_t offset = it->r_offset;
      Symbol &sym = sec.getFile<ELFT>()->getSymbol(it->getSymbol(false));
      if (offset >= sec.getSize()) {
        Err(ctx) << &sec << ": relocation " << type << " offset " << offset
                 << " is outside the section against symbol " << &sym;
        continue;
      }
      if (size > sec.getSize() - offset) {
        Err(ctx) << &sec << ": relocation " << type << " field at offset "
                 << offset << " extends past the end of the section against "
                 << "symbol " << &sym;
        continue;
      }
      if (isTerminalRelocation(type) && (offset & 3) != 0) {
        Err(ctx) << &sec << ": relocation " << type << " field offset "
                 << offset << " is not 4-byte aligned against symbol " << &sym;
        continue;
      }
    }
    rs.scan<ELFT, RelTy>(it, type, rs.getAddend<ELFT>(*it, type));
  }
}

MMIX::RelaxationSection &MMIX::getRelaxationSection(InputSection &sec) const {
  for (RelaxationSection &state : relaxationSections)
    if (state.section == &sec)
      return state;
  ArrayRef<uint8_t> contents = sec.content();
  relaxationSections.push_back({&sec, contents.data(), contents.size(), {}});
  return relaxationSections.back();
}

uint32_t MMIX::getOrCreateCallStub(RelaxationSite &site, uint64_t target,
                                   uint64_t place) const {
  Relocation &rel = site.section->relocs()[site.relocationIndex];
  RelaxationSection &sectionState = getRelaxationSection(*site.section);
  for (uint32_t index : sectionState.stubIndices) {
    CallStub &stub = callStubs[index];
    if (stub.target == rel.sym && stub.addend == rel.addend &&
        isDirectMMIXTransfer(site.section->getVA(stub.offset), place,
                             pc19ValueMask))
      return index;
  }

  uint64_t offset = sectionState.originalSize;
  for (uint32_t index : sectionState.stubIndices)
    offset += callStubs[index].size;
  uint64_t stubVA = site.section->getVA(offset);
  uint8_t size = isDirectMMIXTransfer(target, stubVA, pc27ValueMask) ? 4 : 20;
  std::string name =
      (Twine("__MMIX_call_stub_") + Twine(callStubs.size())).str();
  Defined *symbol = addSyntheticLocal(ctx, ctx.saver.save(name), STT_FUNC,
                                      offset, size, *site.section);
  uint32_t index = callStubs.size();
  callStubs.push_back(
      {site.section, rel.sym, rel.addend, offset, size, symbol});
  sectionState.stubIndices.push_back(index);
  site.section->size = offset + size;
  return index;
}

const MMIX::RelaxationSite *
MMIX::findRelaxationSite(const Relocation &rel) const {
  auto it = relaxationSiteByRelocation.find(&rel);
  return it == relaxationSiteByRelocation.end() ? nullptr
                                                : &relaxationSites[it->second];
}

bool MMIX::relaxOnce(int) const {
  bool changed = orderLinkerAllocatedRegisterContents();
  if (!changed)
    updateRegisterModel();
  changed |= planBasePlusOffsetAllocations();
  if (!relaxationSitesInitialized) {
    SmallVector<InputSection *, 0> storage;
    for (OutputSection *osec : ctx.outputSections) {
      if (!(osec->flags & SHF_EXECINSTR))
        continue;
      for (InputSection *sec : getInputSections(*osec, storage)) {
        ArrayRef<Relocation> rels = sec->relocs();
        for (auto [index, rel] : llvm::enumerate(rels)) {
          if (getRelaxationSequence(rel.type) || isStubbableCall(rel.type)) {
            uint32_t siteIndex = relaxationSites.size();
            relaxationSites.push_back({sec, static_cast<uint32_t>(index)});
            relaxationSiteByRelocation.try_emplace(&rels[index], siteIndex);
          }
        }
      }
    }
    relaxationSitesInitialized = true;
  }

  for (RelaxationSection &sectionState : relaxationSections) {
    uint64_t offset = sectionState.originalSize;
    for (uint32_t index : sectionState.stubIndices) {
      CallStub &stub = callStubs[index];
      stub.offset = offset;
      stub.symbol->value = offset;
      uint64_t target = stub.target->getVA(ctx, stub.addend);
      uint64_t stubVA = stub.section->getVA(offset);
      if (stub.size == 4 &&
          !isDirectMMIXTransfer(target, stubVA, pc27ValueMask)) {
        stub.size = 20;
        stub.symbol->size = 20;
        changed = true;
      }
      offset += stub.size;
    }
    if (sectionState.section->size != offset) {
      sectionState.section->size = offset;
      changed = true;
    }
  }

  for (RelaxationSite &site : relaxationSites) {
    if (site.state == RelaxationState::Invalid)
      continue;

    Relocation &rel = site.section->relocs()[site.relocationIndex];
    uint64_t target = rel.sym->getVA(ctx, rel.addend);
    uint64_t place = site.section->getVA(rel.offset);
    // GETA can materialize an unaligned data address through its expansion.
    if ((target & 3) && rel.type != R_MMIX_GETA) {
      const uint8_t *loc = site.section->content().data() + rel.offset;
      Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << rel.type
               << " against symbol " << rel.sym
               << " has a target that is not 4-byte aligned: 0x"
               << utohexstr(target);
      site.state = RelaxationState::Invalid;
      rel.expr = R_NONE;
      continue;
    }

    uint32_t valueMask = rel.type == R_MMIX_JMP ? pc27ValueMask : pc19ValueMask;
    bool direct = isDirectMMIXTransfer(target, place, valueMask);
    if (isLinux() && ctx.arg.isPic && !direct) {
      Err(ctx) << site.section << ": " << rel.type
               << " cannot expand to an absolute address in a shared object or PIE; "
                  "rebuild with PC-relative literals";
      site.state = RelaxationState::Invalid;
      rel.expr = R_NONE;
      continue;
    }
    if (isStubbableCall(rel.type)) {
      if (site.state == RelaxationState::Pending) {
        if (direct) {
          site.state = RelaxationState::Direct;
        } else {
          site.stubIndex = getOrCreateCallStub(site, target, place);
          site.state = RelaxationState::Stub;
        }
        changed = true;
      } else if (site.state == RelaxationState::Direct && !direct) {
        site.stubIndex = getOrCreateCallStub(site, target, place);
        site.state = RelaxationState::Stub;
        changed = true;
      }

      if (site.state == RelaxationState::Stub) {
        CallStub &stub = callStubs[site.stubIndex];
        uint64_t stubVA = stub.section->getVA(stub.offset);
        if (!isDirectMMIXTransfer(stubVA, place, pc19ValueMask)) {
          const uint8_t *loc = site.section->content().data() + rel.offset;
          Err(ctx) << getErrorLoc(ctx, loc) << "relocation " << rel.type
                   << " against symbol " << rel.sym
                   << " cannot reach its section-end stub";
          site.state = RelaxationState::Invalid;
          rel.expr = R_NONE;
        } else {
          site.callDelta = static_cast<int64_t>(stubVA - place);
        }
      }
      continue;
    }

    if (site.state == RelaxationState::Pending) {
      site.state = direct ? RelaxationState::Direct : RelaxationState::Expanded;
      rel.expr = direct ? R_PC : R_ABS;
      changed = true;
    } else if (site.state == RelaxationState::Direct && !direct) {
      site.state = RelaxationState::Expanded;
      rel.expr = R_ABS;
      changed = true;
    }
  }
  return changed;
}

void MMIX::relocateAlloc(InputSection &sec, uint8_t *buf) const {
  if (isLinux()) {
    for (const Relocation &rel : sec.relocs()) {
      uint64_t target;
      bool pcRelative = true;
      switch (rel.type) {
      case R_MMIX_PC_64:
        target = rel.sym->getVA(ctx);
        break;
      case R_MMIX_GOT_PC64:
      case R_MMIX_GOTTPREL_PC64:
        target = rel.sym->getGotVA(ctx);
        break;
      case R_MMIX_TLSGD_PC64:
        target = ctx.in.got->getGlobalDynAddr(*rel.sym);
        break;
      case R_MMIX_TLSLD_PC64:
        target = ctx.in.got->getTlsIndexVA();
        break;
      case R_MMIX_DTPOFF64:
        target = rel.sym->getVA(ctx);
        pcRelative = false;
        break;
      default:
        continue;
      }
      if (rel.sym->isTls()) {
        if (const auto *d = dyn_cast<Defined>(rel.sym)) {
          const auto *input = dyn_cast_or_null<InputSectionBase>(d->section);
          uint64_t offset = rel.sym->getVA(ctx);
          if (!input || d->value > input->getSize() ||
              d->size > input->getSize() - d->value || !ctx.tlsPhdr ||
              offset > ctx.tlsPhdr->p_memsz ||
              d->size > ctx.tlsPhdr->p_memsz - offset)
            Err(ctx) << getErrorLoc(ctx, sec.content().data() + rel.offset)
                     << "TLS symbol " << rel.sym
                     << " lies outside its section or PT_TLS";
        }
      }
      // Check the mathematical value before the generic path truncates it.
      // PC-relative addends describe slots; DTPOFF addends describe TLS offsets.
      APInt delta = APInt(128, target) + APInt(128, rel.addend, true) -
                    APInt(128, pcRelative ? sec.getVA(rel.offset) : 0);
      if (!delta.isSignedIntN(64))
        Err(ctx) << getErrorLoc(ctx, sec.content().data() + rel.offset)
                 << "relocation " << rel.type
                 << " out of signed 64-bit range";
      if (!isDwarfPC64Field(sec, rel.type) && (sec.getVA(rel.offset) & 7))
        Err(ctx) << getErrorLoc(ctx, sec.content().data() + rel.offset)
                 << "relocation " << rel.type
                 << " literal address is not 8-byte aligned";
    }
  }
  TargetInfo::relocateAlloc(sec, buf);
}

void MMIX::finalizeRelax(int passes) const {
  Log(ctx) << "MMIX relaxation passes: " << passes;
  updateRegisterModel();
  validateLocalAssertions();
  for (RelaxationSection &sectionState : relaxationSections) {
    if (sectionState.stubIndices.empty())
      continue;
    InputSection &sec = *sectionState.section;
    uint8_t *contents = ctx.bAlloc.Allocate<uint8_t>(sec.size);
    memcpy(contents, sectionState.originalContent, sectionState.originalSize);
    for (uint32_t index : sectionState.stubIndices) {
      CallStub &stub = callStubs[index];
      uint8_t *loc = contents + stub.offset;
      uint64_t target = stub.target->getVA(ctx, stub.addend);
      uint64_t stubVA = stub.section->getVA(stub.offset);
      if (stub.size == 4) {
        int64_t delta = static_cast<int64_t>(target - stubVA);
        write32be(loc, encodeMMIXTerminal(0xf0000000, delta, pc27ValueMask));
      } else {
        writeAbsoluteAddress(loc, 255, target);
        write32be(loc + 16, (goImmediateOpcode << 24) | 0xffff00);
      }
    }
    sec.content_ = contents;
  }

  for (RelaxationSite &site : relaxationSites) {
    if (site.state != RelaxationState::Stub)
      continue;
    Relocation &rel = site.section->relocs()[site.relocationIndex];
    uint64_t place = site.section->getVA(rel.offset);
    CallStub &stub = callStubs[site.stubIndex];
    site.callDelta =
        static_cast<int64_t>(stub.section->getVA(stub.offset) - place);
  }

  if (registerContentsOutput)
    registerContentsOutput->addr = uint64_t(firstGlobalRegister) * 8;
  if (isLinux() && !isDynamicOutput())
    validateLinuxLayout();
}

void MMIX::validateLinuxLayout() const {
  constexpr uint64_t userLimit = uint64_t(1) << 63;
  const OutputSection &ehdr = *ctx.out.elfHeader;
  const OutputSection &phdr = *ctx.out.programHeaders;
  for (OutputSection *sec : ctx.outputSections) {
    if (sec->size && (sec->name == registerContentsSectionName ||
                      sec->name == linkerAllocatedRegisterContentsSectionName))
      Err(ctx) << "MMIX Linux does not support register-content output "
               << sec->name;
    if (!(sec->flags & SHF_ALLOC) || !sec->size)
      continue;
    // A section-start override can otherwise alias the file headers without
    // overlapping another ordinary output section.
    if (ehdr.ptLoad && sec->addr < phdr.addr + phdr.size &&
        (sec->addr >= ehdr.addr || sec->size > ehdr.addr - sec->addr))
      Err(ctx) << "MMIX Linux section " << sec->name
               << " overlaps ELF or program headers";
    if (sec->addr >= userLimit || sec->size > userLimit - sec->addr)
      Err(ctx) << "MMIX Linux section " << sec->name
               << " is outside the nonnegative user address range";
    // TLS NOBITS is zero-filled in each thread's allocation, not the process
    // image. Default ELF layout need not give it a PT_LOAD mapping.
    if ((sec->flags & SHF_TLS) && sec->type == SHT_NOBITS) {
      if (!ctx.tlsPhdr)
        Err(ctx) << "MMIX Linux TLS section " << sec->name
                 << " is not in a PT_TLS segment";
      continue;
    }
    if (!sec->ptLoad) {
      Err(ctx) << "MMIX Linux allocated section " << sec->name
               << " is not in a PT_LOAD segment";
      continue;
    }
    uint32_t required = PF_R;
    if (sec->flags & SHF_WRITE)
      required |= PF_W;
    if (sec->flags & SHF_EXECINSTR)
      required |= PF_X;
    if ((sec->ptLoad->p_flags & required) != required)
      Err(ctx) << "MMIX Linux PT_LOAD permissions do not cover section "
               << sec->name;
  }

  if (isDynamicOutput()) {
    if (!ehdr.ptLoad || !(ehdr.ptLoad->p_flags & PF_R) ||
        phdr.ptLoad != ehdr.ptLoad)
      Err(ctx) << "MMIX Linux dynamic images require mapped readable ELF and "
                  "program headers";
    if (llvm::count_if(ctx.phdrs, [](const auto &p) {
          return p->p_type == PT_DYNAMIC;
        }) != 1)
      Err(ctx) << "MMIX Linux dynamic images require exactly one PT_DYNAMIC";
    for (const auto &p : ctx.phdrs) {
      if (p->p_type == PT_INTERP && ctx.arg.shared)
        Err(ctx) << "MMIX Linux shared objects do not support PT_INTERP";
      if (p->p_type == PT_DYNAMIC) {
        const OutputSection *out = ctx.in.dynamic->getParent();
        if (!out || p->firstSec != out || p->lastSec != out ||
            ctx.in.dynamic->getVA() != out->addr ||
            ctx.in.dynamic->getSize() != out->size || !(p->p_flags & PF_R))
          Err(ctx) << "MMIX Linux PT_DYNAMIC must describe the complete "
                      "dynamic table";
      }
      if (p->p_type == PT_PHDR &&
          (p->firstSec != &phdr || p->lastSec != &phdr || !(p->p_flags & PF_R)))
        Err(ctx) << "MMIX Linux PT_PHDR must describe the program header table";
      else if (p->p_type == PT_LOAD &&
               (p->p_flags & (PF_W | PF_X)) == (PF_W | PF_X))
        Err(ctx) << "MMIX Linux dynamic images do not support writable "
                    "executable loads";
    }
    if (llvm::count_if(ctx.phdrs,
                       [](const auto &p) { return p->p_type == PT_PHDR; }) > 1)
      Err(ctx) << "MMIX Linux dynamic images require at most one PT_PHDR";
    if (!ctx.arg.shared) {
      if (llvm::count_if(ctx.phdrs, [](const auto &p) {
            return p->p_type == PT_PHDR;
          }) != 1)
        Err(ctx) << "MMIX Linux dynamic executables require exactly one PT_PHDR";
      if (llvm::count_if(ctx.phdrs, [](const auto &p) {
            return p->p_type == PT_INTERP;
          }) != 1)
        Err(ctx) << "MMIX Linux dynamic executables require exactly one PT_INTERP";
      for (const auto &p : ctx.phdrs)
        if (p->p_type == PT_INTERP) {
          const OutputSection *out = nullptr;
          for (const OutputSection *sec : ctx.outputSections)
            if (sec->name == ".interp")
              out = sec;
          if (!out || p->firstSec != out || p->lastSec != out ||
              out->size != ctx.arg.dynamicLinker.size() + 1 ||
              !out->ptLoad || !(out->ptLoad->p_flags & PF_R) ||
              !(p->p_flags & PF_R) || out->type == SHT_NOBITS)
            Err(ctx) << "MMIX Linux PT_INTERP must describe the complete "
                        "mapped interpreter path";
        }
    }
    if (llvm::count_if(ctx.phdrs,
                       [](const auto &p) {
                         return p->p_type == PT_GNU_STACK &&
                                p->p_flags == (PF_R | PF_W);
                       }) != 1 ||
        llvm::count_if(ctx.phdrs, [](const auto &p) {
          return p->p_type == PT_GNU_STACK;
        }) != 1)
      Err(ctx)
          << "MMIX Linux dynamic images require a non-executable GNU_STACK";
    InputSection *metadata[] = {ctx.in.dynamic.get(), ctx.in.dynSymTab.get(),
                                ctx.in.dynStrTab.get(), ctx.in.hashTab.get(),
                                ctx.in.gnuHashTab.get()};
    for (InputSection *sec : metadata) {
      if (!sec) {
        Err(ctx) << "MMIX Linux dynamic output requires both hash tables";
        continue;
      }
      const OutputSection *out = sec->getParent();
      if (!out || !out->ptLoad || !(out->ptLoad->p_flags & PF_R) ||
          out->type == SHT_NOBITS)
        Err(ctx) << sec
                 << ": MMIX dynamic metadata requires readable file-backed "
                    "PT_LOAD storage";
    }
    for (Symbol *sym : ctx.symtab->getSymbols())
      if (sym->versionId > VER_NDX_GLOBAL)
        Err(ctx) << "MMIX Linux does not support symbol versions: " << sym;
      else if (sym->binding == STB_GNU_UNIQUE)
        Err(ctx) << "MMIX Linux does not support GNU_UNIQUE symbols: " << sym;
    validateDynamicRelocations();
    if (ctx.arg.shared && ctx.arg.entry.empty())
      return;
  }

  uint64_t entry = 0;
  if (Symbol *sym = ctx.symtab->find(ctx.arg.entry))
    entry = sym->getVA(ctx);
  else if (!to_integer(ctx.arg.entry, entry)) {
    Err(ctx) << "MMIX Linux requires a defined entry symbol or address";
    return;
  }
  if (entry >= userLimit || (entry & 3)) {
    Err(ctx) << "MMIX Linux entry must be a nonnegative 4-byte-aligned address";
    return;
  }
  if (llvm::none_of(ctx.outputSections, [&](const OutputSection *sec) {
        return (sec->flags & SHF_ALLOC) && (sec->flags & SHF_EXECINSTR) &&
               sec->ptLoad && (sec->ptLoad->p_flags & PF_X) &&
               entry >= sec->addr && entry - sec->addr < sec->size;
      }))
    Err(ctx) << "MMIX Linux entry is not in a loaded executable section";
}

void MMIX::validateOutput(uint64_t fileSize) const {
  if (!isLinux() || !isDynamicOutput() || ctx.arg.relocatable)
    return;
  validateLinuxLayout();
  constexpr uint64_t userLimit = uint64_t(1) << 63;
  SmallVector<const PhdrEntry *, 4> loads;
  const PhdrEntry *relro = nullptr;
  for (const auto &header : ctx.phdrs) {
    const PhdrEntry &p = *header;
    if (p.p_offset > fileSize || p.p_filesz > fileSize - p.p_offset)
      Err(ctx) << "MMIX Linux program header exceeds output file bounds";
    if (p.p_filesz > p.p_memsz || p.p_vaddr >= userLimit ||
        p.p_memsz > userLimit - p.p_vaddr) {
      Err(ctx) << "MMIX Linux program header has invalid memory extents";
      continue;
    }
    if (p.p_type == PT_GNU_RELRO) {
      if (relro)
        Err(ctx) << "MMIX Linux requires at most one PT_GNU_RELRO";
      relro = &p;
    }
    if (p.p_type != PT_LOAD)
      continue;
    if (p.p_align > 1 &&
        (!isPowerOf2_64(p.p_align) ||
         p.p_vaddr % p.p_align != p.p_offset % p.p_align))
      Err(ctx) << "MMIX Linux PT_LOAD has invalid alignment or congruence";
    if (p.p_vaddr % linuxPageSize != p.p_offset % linuxPageSize)
      Err(ctx) << "MMIX Linux PT_LOAD lacks Linux page congruence";
    if (!(p.p_flags & PF_R) ||
        (p.p_flags & (PF_W | PF_X)) == (PF_W | PF_X))
      Err(ctx) << "MMIX Linux PT_LOAD requires readable non-W+X permissions";
    if (p.p_memsz)
      loads.push_back(&p);
  }
  llvm::sort(loads, [](const auto *a, const auto *b) {
    return a->p_vaddr < b->p_vaddr;
  });
  for (size_t i = 1; i < loads.size(); ++i) {
    const PhdrEntry &prev = *loads[i - 1];
    uint64_t end = alignTo(prev.p_vaddr + prev.p_memsz, linuxPageSize);
    if (alignDown(loads[i]->p_vaddr, linuxPageSize) < end)
      Err(ctx) << "MMIX Linux PT_LOAD ranges overlap after page rounding";
  }
  auto fileBacked = [&](uint64_t va, uint64_t off, uint64_t size) {
    return llvm::any_of(loads, [&](const PhdrEntry *p) {
      return (p->p_flags & PF_R) && va >= p->p_vaddr && off >= p->p_offset &&
             va - p->p_vaddr == off - p->p_offset &&
             off - p->p_offset <= p->p_filesz &&
             size <= p->p_filesz - (off - p->p_offset);
    });
  };
  const OutputSection &ehdr = *ctx.out.elfHeader;
  const OutputSection &phdr = *ctx.out.programHeaders;
  if (ctx.phdrs.empty() || ctx.phdrs.size() >= PN_XNUM ||
      phdr.size < ctx.phdrs.size() * 56 ||
      !fileBacked(ehdr.addr, 0, 64) ||
      !fileBacked(phdr.addr, 64, phdr.size))
    Err(ctx) << "MMIX Linux requires complete file-backed ELF and program headers";
  for (const OutputSection *sec : ctx.outputSections)
    if ((sec->flags & SHF_ALLOC) && !(sec->flags & SHF_EXECINSTR) &&
        sec->size && sec->ptLoad && (sec->ptLoad->p_flags & PF_X))
      Err(ctx) << "MMIX Linux non-code section requires a non-executable load: "
               << sec->name;
  for (const auto &header : ctx.phdrs) {
    const PhdrEntry &p = *header;
    if (p.p_type == PT_PHDR &&
        (p.p_offset != 64 || p.p_vaddr != phdr.addr ||
         p.p_filesz != phdr.size || p.p_memsz != phdr.size))
      Err(ctx) << "MMIX Linux PT_PHDR does not cover the complete header table";
    if ((p.p_type == PT_DYNAMIC || p.p_type == PT_INTERP) &&
        !fileBacked(p.p_vaddr, p.p_offset, p.p_filesz))
      Err(ctx) << "MMIX Linux dynamic metadata lacks file-backed load coverage";
    if (p.p_type == PT_DYNAMIC && (!p.p_filesz || p.p_filesz % 16))
      Err(ctx) << "MMIX Linux PT_DYNAMIC requires complete Elf64_Dyn records";
  }
  if (relro && relro->p_memsz) {
    const auto &r = *relro;
    if (r.p_vaddr % linuxPageSize || r.p_memsz % linuxPageSize ||
        !llvm::any_of(loads, [&](const PhdrEntry *p) {
          return p->p_flags == (PF_R | PF_W) && r.p_vaddr >= p->p_vaddr &&
                 r.p_vaddr - p->p_vaddr <= p->p_memsz &&
                 r.p_memsz <= p->p_memsz - (r.p_vaddr - p->p_vaddr);
        }))
      Err(ctx) << "MMIX Linux RELRO must cover whole pages within a RW/NX load";
    for (const OutputSection *sec : ctx.outputSections)
      if ((sec->flags & (SHF_ALLOC | SHF_WRITE)) == (SHF_ALLOC | SHF_WRITE) &&
          sec->size && !sec->relro && sec->addr < r.p_vaddr + r.p_memsz &&
          r.p_vaddr < sec->addr + sec->size)
        Err(ctx) << "MMIX Linux RELRO covers unrelated mutable section "
                 << sec->name;
  }
}

void MMIX::validateDynamicRelocations() const {
  SmallVector<uint64_t, 0> places;
  auto check = [&](const DynamicReloc &rel) {
    const InputSectionBase &input = *rel.inputSec;
    const OutputSection *out = input.getOutputSection();
    uint64_t offset = rel.offsetInSec;
    bool tls = rel.type == R_MMIX_DTPMOD64 || rel.type == R_MMIX_DTPOFF64 ||
               rel.type == R_MMIX_TPOFF64;
    if (rel.type != R_MMIX_RELATIVE && rel.type != R_MMIX_GLOB_DAT &&
        rel.type != R_MMIX_64 && !tls)
      Err(ctx) << "unsupported MMIX Linux dynamic relocation " << rel.type;
    if (offset > input.getSize() || input.getSize() - offset < 8 || !out ||
        !out->ptLoad || !(out->flags & SHF_WRITE) ||
        (out->flags & SHF_EXECINSTR) ||
        (out->ptLoad->p_flags & (PF_R | PF_W | PF_X)) != (PF_R | PF_W)) {
      Err(ctx) << &input
               << ": MMIX dynamic relocation requires an eight-byte "
                  "writable non-executable PT_LOAD target";
      return;
    }
    // Ordinary pointer initializers are relocated before the TLS template is
    // copied. Runtime TLS slots are not template data, and .tbss has no bytes
    // to copy after relocation.
    if ((out->flags & SHF_TLS) &&
        (tls || input.type == SHT_NOBITS || out->type == SHT_NOBITS)) {
      Err(ctx) << &input
               << ": MMIX TLS template requires an ordinary pointer relocation "
                  "in file-backed storage";
      return;
    }
    uint64_t place = rel.getOffset();
    if (place & 7)
      Err(ctx) << &input
               << ": MMIX dynamic relocation target is not 8-byte aligned";
    places.push_back(place);
    if (tls && rel.needsDynSymIndex() && !rel.sym->isTls())
      Err(ctx) << &input << ": MMIX dynamic TLS relocation requires STT_TLS";
    if (!tls && rel.sym->isTls())
      Err(ctx) << &input
               << ": MMIX ordinary dynamic pointer cannot reference TLS";
    if (rel.type == R_MMIX_RELATIVE) {
      APInt value =
          APInt(128, rel.sym->getVA(ctx)) + APInt(128, rel.addend, true);
      if (rel.needsDynSymIndex() || !value.isIntN(63))
        Err(ctx) << &input
                 << ": invalid MMIX relative relocation symbol or addend";
    }
  };
  for (const DynamicReloc &rel : ctx.in.relaDyn->relativeRelocs)
    check(rel);
  for (const DynamicReloc &rel : ctx.in.relaDyn->relocs)
    check(rel);
  llvm::sort(places);
  for (size_t i = 1; i < places.size(); ++i)
    if (places[i] - places[i - 1] < 8)
      Err(ctx) << "overlapping MMIX Linux dynamic relocation targets";
  if (ctx.in.relaPlt->isNeeded())
    Err(ctx) << "MMIX Linux does not support PLT or copy relocations";
}

void MMIX::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  switch (rel.type) {
  case R_MMIX_TPREL_LO16:
  case R_MMIX_TPREL_ML16:
  case R_MMIX_TPREL_MH16:
  case R_MMIX_TPREL_HI16:
    write16be(loc + 2, val >> getTPRelShift(rel.type));
    return;
  case R_MMIX_NONE:
    return;
  case R_MMIX_8:
  case R_MMIX_PC_8:
    checkMMIXBitfield(ctx, loc, val, 8, rel);
    *loc = val;
    return;
  case R_MMIX_REG_OR_BYTE:
    if (std::optional<uint8_t> reg = resolveRegister(rel, true, loc))
      *loc = *reg;
    return;
  case R_MMIX_REG:
    if (std::optional<uint8_t> reg = resolveRegister(rel, false, loc))
      *loc = *reg;
    return;
  case R_MMIX_BASE_PLUS_OFFSET: {
    const BasePlusOffsetRequest *request = findBasePlusOffsetRequest(rel);
    if (!request || !request->valid || request->baseIndex == UINT32_MAX)
      return;
    uint64_t reg = uint64_t(firstGlobalRegister) + request->baseIndex;
    if (reg > 255) {
      Err(ctx) << getErrorLoc(ctx, loc)
               << "R_MMIX_BASE_PLUS_OFFSET allocation exceeds the global "
                  "register range";
      return;
    }
    write16be(loc, (reg << 8) | request->offset);
    return;
  }
  case R_MMIX_LOCAL:
    return;
  case R_MMIX_16:
  case R_MMIX_PC_16:
    checkMMIXBitfield(ctx, loc, val, 16, rel);
    write16be(loc, val);
    return;
  case R_MMIX_24:
  case R_MMIX_PC_24:
    checkMMIXBitfield(ctx, loc, val, 24, rel);
    write32be(loc, (read32be(loc) & 0xff000000) | (val & 0xffffff));
    return;
  case R_MMIX_32:
  case R_MMIX_PC_32:
    checkMMIXBitfield(ctx, loc, val, 32, rel);
    write32be(loc, val);
    return;
  case R_MMIX_64:
  case R_MMIX_PC_64:
  case R_MMIX_GOT_PC64:
  case R_MMIX_GOTTPREL_PC64:
  case R_MMIX_TLSGD_PC64:
  case R_MMIX_TLSLD_PC64:
  case R_MMIX_DTPOFF64:
  case R_MMIX_DTPMOD64:
  case R_MMIX_TPOFF64:
    write64be(loc, val);
    return;
  case R_MMIX_ADDR19:
    relocateMMIXTerminal(loc, ctx, val, pc19ValueMask, rel);
    return;
  case R_MMIX_ADDR27:
    relocateMMIXTerminal(loc, ctx, val, pc27ValueMask, rel);
    return;
  case R_MMIX_GETA:
  case R_MMIX_CBRANCH:
  case R_MMIX_PUSHJ:
    if (rel.expr == R_PC)
      relocateMMIXTerminal(loc, ctx, val, pc19ValueMask, rel);
    else if (rel.expr == R_ABS)
      relocateMMIXExpanded(loc, val, rel);
    return;
  case R_MMIX_JMP:
    if (rel.expr == R_PC)
      relocateMMIXTerminal(loc, ctx, val, pc27ValueMask, rel);
    else if (rel.expr == R_ABS)
      relocateMMIXExpanded(loc, val, rel);
    return;
  case R_MMIX_PUSHJ_STUBBABLE: {
    const RelaxationSite *site = findRelaxationSite(rel);
    if (!site) {
      Err(ctx) << getErrorLoc(ctx, loc)
               << "missing MMIX stubbable-call relaxation state";
      return;
    }
    uint64_t callValue = site->state == RelaxationState::Stub
                             ? static_cast<uint64_t>(site->callDelta)
                             : val;
    relocateMMIXTerminal(loc, ctx, callValue, pc19ValueMask, rel);
    return;
  }
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unsupported relocation " << rel.type;
  }
}

void elf::setMMIXTargetInfo(Ctx &ctx) { ctx.target.reset(new MMIX(ctx)); }

uint64_t elf::getMMIXTlsTpOffset(Ctx &ctx, const Relocation &rel,
                                 const InputSectionBase &sec) {
  auto error = [&](const Twine &reason) -> uint64_t {
    Err(ctx) << getErrorLoc(ctx, sec.content().data() + rel.offset)
             << "relocation " << rel.type << " against " << rel.sym << ": "
             << reason;
    return 0;
  };
  if (sec.getVA(rel.offset) & 3)
    return error("instruction address is not 4-byte aligned");
  const auto *d = dyn_cast<Defined>(rel.sym);
  PhdrEntry *tls = ctx.tlsPhdr;
  if (!d || !d->section || d->section == &InputSection::discarded ||
      !d->section->isLive() || !rel.sym->isTls())
    return error("requires a defined TLS provider");
  if (!tls || !tls->firstSec || llvm::count_if(ctx.phdrs, [](const auto &p) {
                                  return p->p_type == PT_TLS;
                                }) != 1)
    return error("requires exactly one PT_TLS segment");
  const uint64_t alignment = std::max(uint64_t(tls->p_align), uint64_t(1));
  constexpr uint64_t userLimit = uint64_t(1) << 63;
  if (!isPowerOf2_64(alignment) || tls->p_filesz > tls->p_memsz ||
      tls->p_vaddr >= userLimit || tls->p_memsz > userLimit - tls->p_vaddr ||
      tls->p_offset % alignment != tls->p_vaddr % alignment)
    return error("invalid PT_TLS alignment or extent");
  if (tls->p_filesz && llvm::none_of(ctx.phdrs, [&](const auto &p) {
        return p->p_type == PT_LOAD && (p->p_flags & PF_R) &&
               tls->p_vaddr >= p->p_vaddr && tls->p_offset >= p->p_offset &&
               tls->p_vaddr - p->p_vaddr == tls->p_offset - p->p_offset &&
               tls->p_offset - p->p_offset <= p->p_filesz &&
               tls->p_filesz <= p->p_filesz - (tls->p_offset - p->p_offset);
      }))
    return error("PT_TLS template is not covered by a readable PT_LOAD");
  const OutputSection *os = d->section->getOutputSection();
  if (!os || (os->flags & (SHF_ALLOC | SHF_TLS)) != (SHF_ALLOC | SHF_TLS))
    return error("provider is not in an allocated TLS section");
  const auto *isec = dyn_cast<InputSectionBase>(d->section);
  const uint64_t sectionSize = isec ? isec->getSize() : os->size;
  if (d->value > sectionSize || d->size > sectionSize - d->value)
    return error("TLS symbol lies outside its defining section");
  const uint64_t offset = rel.sym->getVA(ctx);
  if (offset > tls->p_memsz || d->size > tls->p_memsz - offset)
    return error("TLS symbol lies outside PT_TLS");
  constexpr uint64_t headerSize = 16;
  const uint64_t residue = tls->p_vaddr % alignment;
  const uint64_t headerResidue = headerSize % alignment;
  const uint64_t padding = residue >= headerResidue
                               ? residue - headerResidue
                               : alignment - (headerResidue - residue);
  const uint64_t bias = headerSize + padding;
  if (tls->p_memsz > uint64_t(INT64_MAX) - bias)
    return error("TLS allocation extent exceeds PTRDIFF_MAX");
  // Keep the entire mathematical sum until after signed-range validation.
  APInt value = APInt(128, bias) + APInt(128, offset) +
                APInt(128, rel.addend, /*isSigned=*/true);
  if (!value.isSignedIntN(64)) {
    SmallString<40> text;
    value.toString(text, 10, true);
    return error(Twine("TP-relative value ") + text +
                 " is outside signed 64-bit range");
  }
  return value.trunc(64).getZExtValue();
}
