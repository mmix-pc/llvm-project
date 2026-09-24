//===-- MMIXMCCodeEmitter.cpp - Convert MMIX MCInst to bytes --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MMIXBaseInfo.h"
#include "MMIXFixupKinds.h"
#include "MMIXMCExpr.h"
#include "MMIXMCTargetDesc.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/EndianStream.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

namespace {

static bool isExpandedGETARelocation(const MCInst &MI) {
  return MI.getOpcode() == MMIX::GETA && MI.getNumOperands() == 3 &&
         MI.getOperand(2).isImm() &&
         MI.getOperand(2).getImm() == MMIXII::GETARelocationReservedSlots;
}

static bool countGETASymbolTerms(const MCExpr *Expr, unsigned &Symbols) {
  switch (Expr->getKind()) {
  case MCExpr::Constant:
    Symbols = 0;
    return true;
  case MCExpr::SymbolRef:
    Symbols = 1;
    return true;
  case MCExpr::Unary: {
    const auto *Unary = cast<MCUnaryExpr>(Expr);
    if (Unary->getOpcode() != MCUnaryExpr::Plus &&
        Unary->getOpcode() != MCUnaryExpr::Minus)
      return false;
    if (!countGETASymbolTerms(Unary->getSubExpr(), Symbols))
      return false;
    return Unary->getOpcode() == MCUnaryExpr::Plus || Symbols == 0;
  }
  case MCExpr::Binary: {
    const auto *Binary = cast<MCBinaryExpr>(Expr);
    if (Binary->getOpcode() != MCBinaryExpr::Add &&
        Binary->getOpcode() != MCBinaryExpr::Sub)
      return false;
    unsigned LHSSymbols;
    unsigned RHSSymbols;
    if (!countGETASymbolTerms(Binary->getLHS(), LHSSymbols) ||
        !countGETASymbolTerms(Binary->getRHS(), RHSSymbols))
      return false;
    if (Binary->getOpcode() == MCBinaryExpr::Sub && RHSSymbols != 0)
      return false;
    Symbols = LHSSymbols + RHSSymbols;
    return Symbols <= 1;
  }
  default:
    return false;
  }
}

static bool isSplitAddressOpcode(unsigned Opcode) {
  return Opcode == MMIX::SETH || Opcode == MMIX::INCMH ||
         Opcode == MMIX::INCML || Opcode == MMIX::INCL;
}

class MMIXMCCodeEmitter : public MCCodeEmitter {
  const MCInstrInfo &MCII;
  const MCRegisterInfo &MRI;
  MCContext &Ctx;

  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

  unsigned getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

  unsigned getPCRelativeOpValue(const MCInst &MI, unsigned OpNo,
                                SmallVectorImpl<MCFixup> &Fixups,
                                const MCSubtargetInfo &STI) const;

  static MCFixupKind getPCRelativeFixup(const MCInst &MI, uint64_t TSFlags) {
    if (MI.getOpcode() == MMIX::PUSHJ || MI.getOpcode() == MMIX::PUSHJB) {
      if (MI.getFlags() & MMIXII::DirectionNeutralCall)
        return MMIX::fixup_mmix_direction_neutral_call;
      return MMIX::fixup_mmix_call;
    }
    return MMIXII::getPCRelativeWidth(TSFlags) == 16 ? MMIX::fixup_mmix_addr19
                                                     : MMIX::fixup_mmix_addr27;
  }

public:
  MMIXMCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx)
      : MCII(MCII), MRI(*Ctx.getRegisterInfo()), Ctx(Ctx) {}

  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override {
    if (!MMIX::validateTPRELInstruction(MI, STI, Ctx))
      return;
    const bool IsExpandedGETA = isExpandedGETARelocation(MI);
    [[maybe_unused]] const size_t FirstFixup = Fixups.size();
    const uint32_t Word = getBinaryCodeForInstr(MI, Fixups, STI);
    support::endian::write<uint32_t>(CB, Word, llvm::endianness::big);
    if (!IsExpandedGETA)
      return;

    assert(Fixups.size() == FirstFixup + 1 &&
           Fixups.back().getKind() == MMIX::fixup_mmix_geta &&
           "expanded GETA must have one relocation fixup");
    Fixups.back().setLinkerRelaxable();

    MCInst ReservationSlot;
    ReservationSlot.setOpcode(MMIX::SWYM);
    ReservationSlot.addOperand(MCOperand::createImm(0));
    ReservationSlot.addOperand(MCOperand::createImm(0));
    ReservationSlot.addOperand(MCOperand::createImm(0));
    const uint32_t ReservationWord =
        getBinaryCodeForInstr(ReservationSlot, Fixups, STI);
    assert(Fixups.size() == FirstFixup + 1 &&
           "SWYM reservation must not add a fixup");
    for (unsigned I = 0; I != MMIXII::GETARelocationReservedSlots; ++I)
      support::endian::write<uint32_t>(CB, ReservationWord,
                                       llvm::endianness::big);
  }
};

unsigned
MMIXMCCodeEmitter::getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                                     SmallVectorImpl<MCFixup> &Fixups,
                                     const MCSubtargetInfo & /*STI*/) const {
  if (MO.isReg())
    return MRI.getEncodingValue(MO.getReg());
  if (MO.isImm())
    return static_cast<unsigned>(MO.getImm());

  if (MO.isExpr()) {
    const auto *Expr = dyn_cast<MCSpecifierExpr>(MO.getExpr());
    if (Expr && MMIXII::isTPRELSpecifier(Expr->getSpecifier())) {
      const auto Kind =
          static_cast<MCFixupKind>(MMIX::fixup_mmix_tprel_lo +
                                   Expr->getSpecifier() - MMIXII::S_TPREL_LO);
      Fixups.push_back(MCFixup::create(0, Expr, Kind));
      return 0;
    }
  }

  if (MO.isExpr() && isSplitAddressOpcode(MI.getOpcode())) {
    Ctx.reportError(MI.getLoc(),
                    "unresolved MMIX split-address expression is not "
                    "supported; use GETA with '%geta(...)'");
    return 0;
  }

  Ctx.reportError(MI.getLoc(),
                  "unresolved MMIX symbolic instruction operand requires "
                  "relocation support");
  return 0;
}

unsigned
MMIXMCCodeEmitter::getPCRelativeOpValue(const MCInst &MI, unsigned OpNo,
                                        SmallVectorImpl<MCFixup> &Fixups,
                                        const MCSubtargetInfo & /*STI*/) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  const uint64_t TSFlags = MCII.get(MI.getOpcode()).TSFlags;
  const unsigned Width = MMIXII::getPCRelativeWidth(TSFlags);
  assert((Width == 16 || Width == 24) &&
         "unexpected MMIX PC-relative field width");

  if (MO.isExpr()) {
    const MCExpr *Expr = MO.getExpr();
    if (const auto *Specifier = dyn_cast<MCSpecifierExpr>(Expr)) {
      if (Specifier->getSpecifier() != MMIXII::S_GETA) {
        Ctx.reportError(MI.getLoc(),
                        "unsupported MMIX PC-relative expression specifier");
        return 0;
      }
      if (MI.getOpcode() != MMIX::GETA) {
        Ctx.reportError(MI.getLoc(),
                        "'%geta' expression requires a GETA instruction");
        return 0;
      }

      Expr = Specifier->getSubExpr();
      unsigned Symbols;
      if (!countGETASymbolTerms(Expr, Symbols) || Symbols != 1) {
        Ctx.reportError(
            Specifier->getLoc(),
            "expanding GETA requires one symbol plus an optional addend");
        return 0;
      }
      Fixups.push_back(
          MCFixup::create(0, Expr, MMIX::fixup_mmix_geta, /*IsPCRel=*/true));
      return 0;
    }
    Fixups.push_back(MCFixup::create(0, Expr, getPCRelativeFixup(MI, TSFlags),
                                     /*IsPCRel=*/true));
    return 0;
  }

  if (!MO.isImm()) {
    Ctx.reportError(MI.getLoc(),
                    "MMIX PC-relative operand is not an immediate");
    return 0;
  }

  const bool IsBackward = TSFlags & MMIXII::PCRelativeBackward;
  const int64_t Min = IsBackward ? -(int64_t(1) << Width) : 0;
  const int64_t Max =
      IsBackward ? -1 : static_cast<int64_t>((uint64_t(1) << Width) - 1);
  if (MO.getImm() < Min || MO.getImm() > Max) {
    Ctx.reportError(MI.getLoc(), "MMIX PC-relative operand is out of range");
    return 0;
  }

  return static_cast<unsigned>(MO.getImm());
}

#include "MMIXGenMCCodeEmitter.inc"

} // namespace

MCCodeEmitter *llvm::createMMIXMCCodeEmitter(const MCInstrInfo &MCII,
                                             MCContext &Ctx) {
  return new MMIXMCCodeEmitter(MCII, Ctx);
}
