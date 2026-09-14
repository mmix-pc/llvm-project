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
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
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
  int64_t getImplicitAddend(const uint8_t *buf, RelType type) const override;
  template <class ELFT, class RelTy>
  void scanSectionImpl(InputSectionBase &sec, Relocs<RelTy> rels,
                       unsigned shard);
  void scanSection(InputSectionBase &sec, unsigned shard) override {
    elf::scanSection1<MMIX, ELF64BE>(*this, sec, shard);
  }
  bool relaxOnce(int pass) const override;
  void finalizeRelax(int passes) const override;
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
  void validateRelocatableSection(InputSectionBase &sec) const;
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
  if (isLinux()) {
    // Linux fixes rG at process entry; the loader does not initialize GREGs.
    firstGlobalRegister = 230;
    if (!ctx.bitcodeFiles.empty())
      ErrAlways(ctx) << "MMIX Linux does not support bitcode input";
  }
  if (ctx.arg.ekind != ELF64BEKind)
    ErrAlways(ctx) << "MMIX supports only ELF64 big-endian input and output";

  if (ctx.arg.shared)
    ErrAlways(ctx) << "MMIX does not support shared object output";
  else if (ctx.arg.pie)
    ErrAlways(ctx) << "MMIX does not support PIE output";
  else if (ctx.arg.relocatable && !ctx.bitcodeFiles.empty())
    ErrAlways(ctx) << "MMIX does not support relocatable links with bitcode "
                      "input";

  if (!ctx.arg.dynamicLinker.empty())
    ErrAlways(ctx) << "MMIX does not support a dynamic linker";
  if (ctx.arg.oFormatBinary)
    ErrAlways(ctx) << "MMIX lld supports only ELF output";
  if (ctx.arg.osabi != ELFOSABI_NONE)
    ErrAlways(ctx) << "MMIX supports only the System V ELF OSABI";
  if (!ctx.sharedFiles.empty())
    ErrAlways(ctx) << "MMIX does not support dynamic shared object inputs";
  for (InputFile *file : ctx.objectFiles)
    if (file->abiVersion != 0)
      ErrAlways(ctx) << file << ": unsupported MMIX ELF ABI version "
                     << static_cast<unsigned>(file->abiVersion);

  collectRegisterModel();
}

void MMIX::initTargetSpecificSections() {
  StringRef name = ctx.script->hasSectionsCommand
                       ? linkerAllocatedRegisterContentsSectionName
                       : registerContentsSectionName;
  linkerAllocatedRegisterContents =
      std::make_unique<MMIXLinkerAllocatedRegisterSection>(ctx, name);
  ctx.inputSections.push_back(linkerAllocatedRegisterContents.get());

  if (ctx.arg.relocatable)
    for (ELFFileBase *file : ctx.objectFiles)
      for (InputSectionBase *section : file->getSections())
        if (section && section != &InputSection::discarded &&
            section->kind() == SectionBase::Regular)
          validateRelocatableSection(*section);
}

void MMIX::validateRelocatableSection(InputSectionBase &sec) const {
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
    Symbol &sym = sec.getFile<ELF64BE>()->getSymbol(rela.getSymbol(false));
    const uint8_t *loc = contents.data();
    if (rela.r_offset < contents.size())
      loc += rela.r_offset;
    (void)getRelExpr(type, sym, loc);
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
}

void MMIX::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  switch (rel.type) {
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
