//===-- MMIXAsmBackend.cpp - MMIX assembler backend -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MMIXBaseInfo.h"
#include "MMIXFixupKinds.h"
#include "MMIXMCTargetDesc.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Endian.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

namespace {

static bool isUnrelaxedGETARelocation(unsigned Opcode,
                                      ArrayRef<MCOperand> Operands) {
  if (Opcode != MMIX::GETA || Operands.size() != 2 ||
      !Operands[1].isExpr())
    return false;
  const auto *Specifier = dyn_cast<MCSpecifierExpr>(Operands[1].getExpr());
  return Specifier && Specifier->getSpecifier() == MMIXII::S_GETA;
}

static bool isDirectGETAValue(uint64_t Value) {
  const int64_t Delta = static_cast<int64_t>(Value);
  constexpr int64_t Min = -(int64_t(1) << 18);
  constexpr int64_t Max = (int64_t(1) << 18) - 4;
  return (Delta & 3) == 0 && Delta >= Min && Delta <= Max;
}

class MMIXAsmBackend : public MCAsmBackend {
  void addRelocation(const MCFragment &F, const MCFixup &Fixup,
                     const MCValue &Target, uint64_t Value) {
    const MCSymbol *Sub = Target.getSubSym();
    if (Sub && Sub->isInSection() && &Sub->getSection() == F.getParent()) {
      // The generic ELF writer cannot fold an already PC-relative difference.
      // Undefined and cross-section subtrahends retain its own diagnostics.
      const char *Message = nullptr;
      switch (Fixup.getKind()) {
      case MMIX::fixup_mmix_addr19:
        Message = "MMIX 19-bit terminal control relocation does not support "
                  "symbol differences";
        break;
      case MMIX::fixup_mmix_addr27:
        Message = "MMIX 27-bit terminal control relocation does not support "
                  "symbol differences";
        break;
      case MMIX::fixup_mmix_call:
      case MMIX::fixup_mmix_direction_neutral_call:
        Message = "MMIX stubbable call relocations do not support symbol "
                  "differences";
        break;
      case MMIX::fixup_mmix_geta:
        Message = "R_MMIX_GETA does not support symbol differences";
        break;
      case MMIX::fixup_mmix_data_24:
      case MMIX::fixup_mmix_pcrel_24:
        Message = "MMIX 24-in-32 data relocations do not support symbol "
                  "differences";
        break;
      case MMIX::fixup_mmix_branch_forward:
      case MMIX::fixup_mmix_branch_backward:
      case MMIX::fixup_mmix_jump_forward:
      case MMIX::fixup_mmix_jump_backward:
        Message = "MMIX PC-relative relocation does not support symbol "
                  "differences";
        break;
      default:
        break;
      }
      if (Message) {
        getContext().reportError(Fixup.getLoc(), Message);
        return;
      }
    }
    maybeAddReloc(F, Fixup, Target, Value, /*IsResolved=*/false);
  }

public:
  MMIXAsmBackend() : MCAsmBackend(llvm::endianness::big) {}
  ~MMIXAsmBackend() override = default;

  std::optional<MCFixupKind> getFixupKind(StringRef Name) const override {
    if (Name == "R_MMIX_GETA")
      return static_cast<MCFixupKind>(MMIX::fixup_mmix_geta);

    const unsigned Type = StringSwitch<unsigned>(Name)
                              .Case("R_MMIX_GETA_1", ELF::R_MMIX_GETA_1)
                              .Case("R_MMIX_GETA_2", ELF::R_MMIX_GETA_2)
                              .Case("R_MMIX_GETA_3", ELF::R_MMIX_GETA_3)
                              .Case("R_MMIX_CBRANCH", ELF::R_MMIX_CBRANCH)
                              .Case("R_MMIX_CBRANCH_J", ELF::R_MMIX_CBRANCH_J)
                              .Case("R_MMIX_CBRANCH_1", ELF::R_MMIX_CBRANCH_1)
                              .Case("R_MMIX_CBRANCH_2", ELF::R_MMIX_CBRANCH_2)
                              .Case("R_MMIX_CBRANCH_3", ELF::R_MMIX_CBRANCH_3)
                              .Case("R_MMIX_PUSHJ", ELF::R_MMIX_PUSHJ)
                              .Case("R_MMIX_PUSHJ_1", ELF::R_MMIX_PUSHJ_1)
                              .Case("R_MMIX_PUSHJ_2", ELF::R_MMIX_PUSHJ_2)
                              .Case("R_MMIX_PUSHJ_3", ELF::R_MMIX_PUSHJ_3)
                              .Case("R_MMIX_JMP", ELF::R_MMIX_JMP)
                              .Case("R_MMIX_JMP_1", ELF::R_MMIX_JMP_1)
                              .Case("R_MMIX_JMP_2", ELF::R_MMIX_JMP_2)
                              .Case("R_MMIX_JMP_3", ELF::R_MMIX_JMP_3)
                              .Default(-1u);
    if (Type != -1u)
      return static_cast<MCFixupKind>(FirstLiteralRelocationKind + Type);
    return MCAsmBackend::getFixupKind(Name);
  }

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data, uint64_t Value,
                  bool IsResolved) override {
    if (mc::isRelocRelocation(Fixup.getKind())) {
      const unsigned Type = Fixup.getKind() - FirstLiteralRelocationKind;
      switch (Type) {
      case ELF::R_MMIX_GETA:
        getContext().reportError(
            Fixup.getLoc(),
            "R_MMIX_GETA requires an assembler-owned 16-byte reservation");
        return;
      case ELF::R_MMIX_CBRANCH:
        getContext().reportError(
            Fixup.getLoc(),
            "R_MMIX_CBRANCH requires an assembler-owned 24-byte reservation");
        return;
      case ELF::R_MMIX_PUSHJ:
        getContext().reportError(
            Fixup.getLoc(),
            "R_MMIX_PUSHJ requires an assembler-owned 20-byte reservation");
        return;
      case ELF::R_MMIX_JMP:
        getContext().reportError(
            Fixup.getLoc(),
            "R_MMIX_JMP requires an assembler-owned 20-byte reservation");
        return;
      case ELF::R_MMIX_GETA_1:
      case ELF::R_MMIX_GETA_2:
      case ELF::R_MMIX_GETA_3:
      case ELF::R_MMIX_CBRANCH_J:
      case ELF::R_MMIX_CBRANCH_1:
      case ELF::R_MMIX_CBRANCH_2:
      case ELF::R_MMIX_CBRANCH_3:
      case ELF::R_MMIX_PUSHJ_1:
      case ELF::R_MMIX_PUSHJ_2:
      case ELF::R_MMIX_PUSHJ_3:
      case ELF::R_MMIX_JMP_1:
      case ELF::R_MMIX_JMP_2:
      case ELF::R_MMIX_JMP_3:
        getContext().reportError(
            Fixup.getLoc(),
            "GNU MMIX intermediate relaxation relocations cannot be emitted "
            "directly");
        return;
      default:
        getContext().reportError(Fixup.getLoc(),
                                 "raw MMIX relocation is not supported");
        return;
      }
    }

    if (Fixup.getKind() == MMIX::fixup_mmix_geta) {
      if (!Fixup.isPCRel()) {
        getContext().reportError(
            Fixup.getLoc(),
            "R_MMIX_GETA requires an assembler-owned 16-byte reservation");
        return;
      }
      if (Fixup.isLinkerRelaxable()) {
        addRelocation(F, Fixup, Target, Value);
        return;
      }
      if (!IsResolved) {
        getContext().reportError(
            Fixup.getLoc(),
            "unresolved GETA relocation is missing its reservation");
        return;
      }
      if ((Value & 3) != 0) {
        getContext().reportError(
            Fixup.getLoc(), "MMIX GETA target is not instruction aligned");
        return;
      }
      if (!isDirectGETAValue(Value)) {
        getContext().reportError(Fixup.getLoc(),
                                 "MMIX GETA target is not directly encodable");
        return;
      }

      const int64_t Delta = static_cast<int64_t>(Value);
      const uint32_t Encoded = static_cast<uint32_t>(Delta / 4) & 0xffff;
      assert(Fixup.getOffset() + 4 <= F.getSize() &&
             "invalid direct MMIX GETA fixup offset");
      uint32_t Word = support::endian::read32be(Data);
      constexpr uint32_t BackwardOpcodeBit = uint32_t(1) << 24;
      Word = (Word & ~(BackwardOpcodeBit | 0xffff)) |
             (Delta < 0 ? BackwardOpcodeBit : 0) | Encoded;
      support::endian::write32be(Data, Word);
      return;
    }

    if (!IsResolved) {
      addRelocation(F, Fixup, Target, Value);
      return;
    }

    const MCFixupKind Kind = Fixup.getKind();
    if (Kind < FirstTargetFixupKind) {
      unsigned Size;
      switch (Kind) {
      case FK_Data_1:
        Size = 1;
        break;
      case FK_Data_2:
        Size = 2;
        break;
      case FK_Data_4:
        Size = 4;
        break;
      case FK_Data_8:
        Size = 8;
        break;
      default:
        getContext().reportError(Fixup.getLoc(),
                                 "unsupported resolved MMIX data fixup");
        return;
      }
      assert(Fixup.getOffset() + Size <= F.getSize() &&
             "invalid MMIX data fixup offset");
      for (unsigned I = 0; I != Size; ++I)
        Data[I] |= static_cast<uint8_t>(Value >> ((Size - I - 1) * 8));
      return;
    }

    if (Kind == MMIX::fixup_mmix_data_24 || Kind == MMIX::fixup_mmix_pcrel_24) {
      assert(Fixup.getOffset() + 4 <= F.getSize() &&
             "invalid MMIX 24-in-32 data fixup offset");
      if (Value > 0xffffff && Value < 0xffffffffff000000ULL) {
        getContext().reportError(Fixup.getLoc(),
                                 "MMIX 24-bit data fixup is out of range");
        return;
      }

      uint32_t Word = support::endian::read32be(Data);
      Word = (Word & 0xff000000) | (static_cast<uint32_t>(Value) & 0xffffff);
      support::endian::write32be(Data, Word);
      return;
    }

    int64_t Delta = static_cast<int64_t>(Value);
    const bool IsTerminal = Kind == MMIX::fixup_mmix_addr19 ||
                            Kind == MMIX::fixup_mmix_addr27;
    const bool IsDirectionNeutralCall =
        Kind == MMIX::fixup_mmix_direction_neutral_call;
    const bool IsCall = Kind == MMIX::fixup_mmix_call || IsDirectionNeutralCall;
    const bool Uses16BitDisplacement =
        Kind == MMIX::fixup_mmix_branch_forward ||
        Kind == MMIX::fixup_mmix_branch_backward ||
        Kind == MMIX::fixup_mmix_addr19 || IsCall;
    uint32_t Word = support::endian::read32be(Data);
    constexpr uint32_t BackwardOpcodeBit = uint32_t(1) << 24;
    const bool EncodedBackward =
        Kind == MMIX::fixup_mmix_branch_backward ||
        Kind == MMIX::fixup_mmix_jump_backward ||
        ((IsTerminal || IsCall) && (Word & BackwardOpcodeBit) != 0);
    const unsigned Width = Uses16BitDisplacement ? 16 : 24;
    const char *TerminalField = Uses16BitDisplacement ? "19-bit" : "27-bit";

    if ((Delta & 3) != 0) {
      if (IsTerminal)
        getContext().reportError(
            Fixup.getLoc(), Twine("MMIX ") + TerminalField +
                                " terminal target is not instruction aligned");
      else if (IsCall)
        getContext().reportError(
            Fixup.getLoc(),
            "MMIX direct call target is not instruction aligned");
      else
        getContext().reportError(
            Fixup.getLoc(),
            "MMIX PC-relative fixup is not instruction aligned");
      return;
    }
    Delta /= 4;
    const bool IsBackward =
        IsDirectionNeutralCall ? Delta < 0 : EncodedBackward;
    const int64_t Min = IsBackward ? -(int64_t(1) << Width) : 0;
    const int64_t Max = IsBackward ? -1 : (int64_t(1) << Width) - 1;
    if (!IsDirectionNeutralCall && (IsTerminal || IsCall) &&
        ((IsBackward && Delta >= 0) || (!IsBackward && Delta < 0))) {
      if (IsTerminal)
        getContext().reportError(
            Fixup.getLoc(), Twine("MMIX ") + TerminalField +
                                " terminal target direction does not match "
                                "instruction");
      else
        getContext().reportError(
            Fixup.getLoc(),
            "MMIX direct call target direction does not match instruction");
      return;
    }
    if (Delta < Min || Delta > Max) {
      if (IsTerminal)
        getContext().reportError(
            Fixup.getLoc(), Twine("MMIX ") + TerminalField +
                                " terminal target is out of range");
      else if (IsCall)
        getContext().reportError(Fixup.getLoc(),
                                 "MMIX direct call target is out of range");
      else
        getContext().reportError(Fixup.getLoc(),
                                 "MMIX PC-relative fixup is out of range");
      return;
    }

    if (IsDirectionNeutralCall) {
      Word &= ~BackwardOpcodeBit;
      if (IsBackward)
        Word |= BackwardOpcodeBit;
    }

    const uint32_t Encoded =
        static_cast<uint32_t>(Delta) & ((uint32_t(1) << Width) - 1);
    const unsigned Shift = 0;
    const uint32_t Mask = ((uint32_t(1) << Width) - 1) << Shift;
    Word = (Word & ~(Mask)) | (Encoded << Shift);
    support::endian::write32be(Data, Word);
  }

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createMMIXELFObjectWriter();
  }

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override {
    static const MCFixupKindInfo Infos[MMIX::NumTargetFixupKinds] = {
        {"fixup_mmix_branch_forward", 0, 16, 0},
        {"fixup_mmix_branch_backward", 0, 16, 0},
        {"fixup_mmix_jump_forward", 0, 24, 0},
        {"fixup_mmix_jump_backward", 0, 24, 0},
        {"fixup_mmix_addr19", 0, 16, 0},
        {"fixup_mmix_addr27", 0, 24, 0},
        {"fixup_mmix_call", 0, 16, 0},
        {"fixup_mmix_direction_neutral_call", 0, 16, 0},
        {"fixup_mmix_data_24", 0, 24, 0},
        {"fixup_mmix_pcrel_24", 0, 24, 0},
        {"fixup_mmix_geta", 0, 16, 0},
    };

    if (mc::isRelocRelocation(Kind))
      return {};
    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);
    return Infos[Kind - FirstTargetFixupKind];
  }

  std::optional<bool> evaluateFixup(const MCFragment &F, MCFixup &Fixup,
                                    MCValue &Target,
                                    uint64_t &Value) override {
    const MCSymbol *Add = Target.getAddSym();
    const MCSymbol *Sub = Target.getSubSym();
    if (Add && Sub && Add->isDefined() && Sub->isDefined() &&
        &Add->getSection() == &Sub->getSection()) {
      // MMIX linker relaxation rewrites a fixed 16-byte GETA reservation, so
      // it cannot change same-section symbol differences.
      Value = Target.getConstant() + Asm->getSymbolOffset(*Add) -
              Asm->getSymbolOffset(*Sub);
      return true;
    }

    if (Fixup.getKind() != MMIX::fixup_mmix_geta || !Target.isAbsolute())
      return {};
    Value = Target.getConstant();
    if (Fixup.isPCRel())
      Value -= Asm->getFragmentOffset(F) + Fixup.getOffset();
    return true;
  }

  std::pair<bool, bool> relaxLEB128(MCFragment &F,
                                    int64_t &Value) const override {
    // Fixed-size relaxation reservations keep layout-known differences stable.
    return {F.getLEBValue().evaluateKnownAbsolute(Value, *Asm), false};
  }

  bool relaxDwarfCFA(MCFragment &F) const override {
    int64_t Value;
    if (F.getDwarfAddrDelta().evaluateAsAbsolute(Value, *Asm))
      return false;
    if (!F.getDwarfAddrDelta().evaluateKnownAbsolute(Value, *Asm))
      return false;

    SmallVector<char, 8> Data;
    MCDwarfFrameEmitter::encodeAdvanceLoc(getContext(), Value, Data);
    F.setVarContents(Data);
    F.clearVarFixups();
    return true;
  }

  bool mayNeedRelaxation(unsigned Opcode, ArrayRef<MCOperand> Operands,
                         const MCSubtargetInfo &) const override {
    return isUnrelaxedGETARelocation(Opcode, Operands);
  }

  bool fixupNeedsRelaxationAdvanced(const MCFragment &, const MCFixup &Fixup,
                                    const MCValue &, uint64_t Value,
                                    bool Resolved) const override {
    assert(Fixup.getKind() == MMIX::fixup_mmix_geta &&
           "unexpected MMIX relaxable fixup");
    return !Resolved || ((Value & 3) == 0 && !isDirectGETAValue(Value));
  }

  void relaxInstruction(MCInst &Inst,
                        const MCSubtargetInfo &) const override {
    assert(isUnrelaxedGETARelocation(Inst.getOpcode(), Inst.getOperands()) &&
           "unexpected MMIX instruction relaxation");
    Inst.addOperand(
        MCOperand::createImm(MMIXII::GETARelocationReservedSlots));
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *) const override {
    if (Count % 4 != 0)
      return false;
    static constexpr char Zero[4] = {0, 0, 0, 0};
    for (uint64_t I = 0; I < Count; I += 4)
      OS.write(Zero, sizeof(Zero));
    return true;
  }
};

} // namespace

MCAsmBackend *llvm::createMMIXAsmBackend(const Target &,
                                         const MCSubtargetInfo &,
                                         const MCRegisterInfo &,
                                         const MCTargetOptions &) {
  return new MMIXAsmBackend();
}
