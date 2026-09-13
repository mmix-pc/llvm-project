//===-- MMIXSubtarget.cpp - MMIX Subtarget Information --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MMIXSubtarget.h"
#include "llvm/CodeGen/LibcallLoweringInfo.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "mmix-subtarget"

#define GET_SUBTARGETINFO_ENUM
#include "MMIXGenSubtargetInfo.inc"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "MMIXGenSubtargetInfo.inc"

void MMIXSubtarget::anchor() {}

MMIXSubtarget &MMIXSubtarget::initializeSubtargetDependencies(StringRef CPU,
                                                              StringRef FS) {
  StringRef CPUName = CPU.empty() ? "generic" : CPU;
  ParseSubtargetFeatures(CPUName, CPUName, FS);
  return *this;
}

MMIXSubtarget::MMIXSubtarget(const Triple &TT, StringRef CPU, StringRef FS,
                             const TargetMachine &TM)
    : MMIXGenSubtargetInfo(TT, CPU.empty() ? "generic" : CPU,
                           /*TuneCPU=*/CPU.empty() ? "generic" : CPU, FS),
      InstrInfo(initializeSubtargetDependencies(CPU, FS)), FrameLowering(),
      TLInfo(TM, *this) {}

void MMIXSubtarget::initLibcallLoweringInfo(LibcallLoweringInfo &Info) const {
  if (TLInfo.getTargetMachine().getExceptionModel() == ExceptionHandling::DwarfCFI)
    Info.setLibcallImpl(RTLIB::UNWIND_RESUME, RTLIB::impl__Unwind_Resume);

  Info.setLibcallImpl(RTLIB::MEMCPY, RTLIB::impl_memcpy);
  Info.setLibcallImpl(RTLIB::MEMMOVE, RTLIB::impl_memmove);
  Info.setLibcallImpl(RTLIB::MEMSET, RTLIB::impl_memset);

  // Wide integer helpers use the same two-octa scalar ABI as ordinary IR calls.
  Info.setLibcallImpl(RTLIB::MUL_I128, RTLIB::impl___multi3);
  Info.setLibcallImpl(RTLIB::MULO_I128, RTLIB::impl___muloti4);
  Info.setLibcallImpl(RTLIB::SDIV_I128, RTLIB::impl___divti3);
  Info.setLibcallImpl(RTLIB::UDIV_I128, RTLIB::impl___udivti3);
  Info.setLibcallImpl(RTLIB::SREM_I128, RTLIB::impl___modti3);
  Info.setLibcallImpl(RTLIB::UREM_I128, RTLIB::impl___umodti3);
  Info.setLibcallImpl(RTLIB::SINTTOFP_I128_F32, RTLIB::impl___floattisf);
  Info.setLibcallImpl(RTLIB::SINTTOFP_I128_F64, RTLIB::impl___floattidf);
  Info.setLibcallImpl(RTLIB::UINTTOFP_I128_F32, RTLIB::impl___floatuntisf);
  Info.setLibcallImpl(RTLIB::UINTTOFP_I128_F64, RTLIB::impl___floatuntidf);
  Info.setLibcallImpl(RTLIB::FPTOSINT_F32_I128, RTLIB::impl___fixsfti);
  Info.setLibcallImpl(RTLIB::FPTOSINT_F64_I128, RTLIB::impl___fixdfti);
  Info.setLibcallImpl(RTLIB::FPTOUINT_F32_I128, RTLIB::impl___fixunssfti);
  Info.setLibcallImpl(RTLIB::FPTOUINT_F64_I128, RTLIB::impl___fixunsdfti);

  Info.setLibcallImpl(RTLIB::REM_F32, RTLIB::impl_fmodf);
  Info.setLibcallImpl(RTLIB::REM_F64, RTLIB::impl_fmod);
  Info.setLibcallImpl(RTLIB::FMA_F32, RTLIB::impl_fmaf);
  Info.setLibcallImpl(RTLIB::FMA_F64, RTLIB::impl_fma);
  Info.setLibcallImpl(RTLIB::ROUND_F32, RTLIB::impl_roundf);
  Info.setLibcallImpl(RTLIB::ROUND_F64, RTLIB::impl_round);
  Info.setLibcallImpl(RTLIB::NEARBYINT_F32, RTLIB::impl_nearbyintf);
  Info.setLibcallImpl(RTLIB::NEARBYINT_F64, RTLIB::impl_nearbyint);

  Info.setLibcallImpl(RTLIB::ATOMIC_LOAD, RTLIB::impl___atomic_load);
  Info.setLibcallImpl(RTLIB::ATOMIC_STORE, RTLIB::impl___atomic_store);
  Info.setLibcallImpl(RTLIB::ATOMIC_EXCHANGE, RTLIB::impl___atomic_exchange);
  Info.setLibcallImpl(RTLIB::ATOMIC_COMPARE_EXCHANGE,
                      RTLIB::impl___atomic_compare_exchange);

  Info.setLibcallImpl(RTLIB::STACKPROTECTOR_CHECK_FAIL,
                      RTLIB::impl___stack_chk_fail);
  Info.setLibcallImpl(RTLIB::STACK_CHECK_GUARD,
                      RTLIB::impl___stack_chk_guard);
}
