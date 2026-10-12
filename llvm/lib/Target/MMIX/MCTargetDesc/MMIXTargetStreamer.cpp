//===-- MMIXTargetStreamer.cpp - MMIX target streamer --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MMIXTargetStreamer.h"
#include "MMIXBaseInfo.h"
#include "MMIXFixupKinds.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCSection.h"
#include "llvm/Support/FormattedStream.h"
#include <array>

using namespace llvm;

namespace {

class MMIXTargetObjectStreamer final : public MMIXTargetStreamer {
public:
  explicit MMIXTargetObjectStreamer(MCStreamer &S) : MMIXTargetStreamer(S) {}

  void emitGOTPC64(const MCExpr *Value, SMLoc Loc) override {
    auto &S = static_cast<MCObjectStreamer &>(Streamer);
    S.visitUsedExpr(*Value);
    S.getCurrentSectionOnly()->ensureMinAlignment(Align(8));
    S.ensureHeadroom(8);
    // Preserve symbol identity even for a definition in this section: the
    // linker allocates the GOT entry, not the assembler.
    Value = MCSpecifierExpr::create(Value, MMIXII::S_GOT_PC64, S.getContext(), Loc);
    S.getCurrentFragment()->addFixup(MCFixup::create(
        S.getCurFragSize(), Value, MMIX::fixup_mmix_got_pc64, true));
    S.appendContents(std::array<char, 8>{});
  }

  void emitData24(uint8_t HighByte, const MCExpr *Value, bool IsPCRel,
                  SMLoc Loc) override {
    auto &S = static_cast<MCObjectStreamer &>(Streamer);
    S.ensureHeadroom(4);
    const MCFixupKind Kind =
        IsPCRel ? MMIX::fixup_mmix_pcrel_24 : MMIX::fixup_mmix_data_24;
    Value = MCUnaryExpr::create(MCUnaryExpr::Plus, Value, S.getContext(), Loc);
    S.getCurrentFragment()->addFixup(
        MCFixup::create(S.getCurFragSize(), Value, Kind, IsPCRel));
    const std::array<char, 4> Contents = {static_cast<char>(HighByte), 0, 0, 0};
    S.appendContents(Contents);
  }
};

class MMIXTargetAsmStreamer final : public MMIXTargetStreamer {
  formatted_raw_ostream &OS;

public:
  MMIXTargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS)
      : MMIXTargetStreamer(S), OS(OS) {}

  void emitGOTPC64(const MCExpr *Value, SMLoc) override {
    OS << "\t.mmix_got_pc64\t";
    Streamer.getContext().getAsmInfo().printExpr(OS, *Value);
    OS << '\n';
  }

  void emitData24(uint8_t HighByte, const MCExpr *Value, bool IsPCRel,
                  SMLoc) override {
    OS << (IsPCRel ? "\t.mmix_pc_24\t" : "\t.mmix_24\t")
       << static_cast<unsigned>(HighByte) << ", ";
    Streamer.getContext().getAsmInfo().printExpr(OS, *Value);
    OS << '\n';
  }
};

class MMIXTargetNullStreamer final : public MMIXTargetStreamer {
public:
  explicit MMIXTargetNullStreamer(MCStreamer &S) : MMIXTargetStreamer(S) {}

  void emitGOTPC64(const MCExpr *, SMLoc) override {}

  void emitData24(uint8_t, const MCExpr *, bool, SMLoc) override {}
};

} // namespace

MCTargetStreamer *
llvm::createMMIXObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &) {
  return new MMIXTargetObjectStreamer(S);
}

MCTargetStreamer *llvm::createMMIXAsmTargetStreamer(MCStreamer &S,
                                                    formatted_raw_ostream &OS,
                                                    MCInstPrinter *) {
  return new MMIXTargetAsmStreamer(S, OS);
}

MCTargetStreamer *llvm::createMMIXNullTargetStreamer(MCStreamer &S) {
  return new MMIXTargetNullStreamer(S);
}
