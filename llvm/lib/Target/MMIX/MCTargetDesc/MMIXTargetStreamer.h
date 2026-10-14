//===-- MMIXTargetStreamer.h - MMIX target streamer -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MMIX_MCTARGETDESC_MMIXTARGETSTREAMER_H
#define LLVM_LIB_TARGET_MMIX_MCTARGETDESC_MMIXTARGETSTREAMER_H

#include "llvm/MC/MCStreamer.h"
#include <cstdint>

namespace llvm {

class MCExpr;
class MCInstPrinter;
class MCSubtargetInfo;
class formatted_raw_ostream;

class MMIXTargetStreamer : public MCTargetStreamer {
public:
  explicit MMIXTargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}

  virtual void emitData24(uint8_t HighByte, const MCExpr *Value, bool IsPCRel,
                          SMLoc Loc) = 0;
  void emitGOTPC64(const MCExpr *Value, SMLoc Loc);
  virtual void emitPCRelLiteral64(const MCExpr *Value, unsigned Specifier,
                                SMLoc Loc) = 0;
};

MCTargetStreamer *createMMIXObjectTargetStreamer(MCStreamer &S,
                                                 const MCSubtargetInfo &STI);
MCTargetStreamer *createMMIXAsmTargetStreamer(MCStreamer &S,
                                              formatted_raw_ostream &OS,
                                              MCInstPrinter *IP);
MCTargetStreamer *createMMIXNullTargetStreamer(MCStreamer &S);

} // namespace llvm

#endif
