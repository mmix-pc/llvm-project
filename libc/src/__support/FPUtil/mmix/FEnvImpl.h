//===-- MMIX floating point environment --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_FPUTIL_MMIX_FENVIMPL_H
#define LLVM_LIBC_SRC___SUPPORT_FPUTIL_MMIX_FENVIMPL_H

#include "hdr/fenv_macros.h"
#include "hdr/types/fenv_t.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace fputil {

extern "C" void __llvm_libc_mmix_set_fenv(unsigned long);

struct FEnv {
  // rA contains DVWIOUZX events, matching enables eight bits above them,
  // and the rounding selector in bits 16-17. Preserve integer D/V/W state.
  static constexpr unsigned long FP_MASK = 0x1f;
  static constexpr unsigned long ROUND_MASK = 3UL << 16;
  static constexpr unsigned long ENV_MASK = FP_MASK | (FP_MASK << 8) | ROUND_MASK;

  LIBC_INLINE static unsigned long read() {
    unsigned long value;
    LIBC_INLINE_ASM("GET %0,rA" : "=r"(value) : : "memory");
    return value;
  }
  LIBC_INLINE static void write(unsigned long value) {
    // rA writes are kept in an out-of-line leaf, outside function inline asm.
    __llvm_libc_mmix_set_fenv(value);
  }
  LIBC_INLINE static constexpr unsigned long to_bits(int flags) {
    return ((flags & FE_INVALID) ? 16 : 0) | ((flags & FE_OVERFLOW) ? 8 : 0) |
           ((flags & FE_UNDERFLOW) ? 4 : 0) | ((flags & FE_DIVBYZERO) ? 2 : 0) |
           ((flags & FE_INEXACT) ? 1 : 0);
  }
  LIBC_INLINE static constexpr int to_flags(unsigned long bits) {
    return ((bits & 16) ? FE_INVALID : 0) | ((bits & 8) ? FE_OVERFLOW : 0) |
           ((bits & 4) ? FE_UNDERFLOW : 0) | ((bits & 2) ? FE_DIVBYZERO : 0) |
           ((bits & 1) ? FE_INEXACT : 0);
  }
};

LIBC_INLINE int clear_except(int flags) {
  FEnv::write(FEnv::read() & ~FEnv::to_bits(flags));
  return 0;
}
LIBC_INLINE int test_except(int flags) {
  return FEnv::to_flags(FEnv::read()) & flags;
}
LIBC_INLINE int set_except(int flags) {
  FEnv::write(FEnv::read() | FEnv::to_bits(flags));
  return 0;
}
LIBC_INLINE int get_except() { return FEnv::to_flags(FEnv::read() >> 8); }
LIBC_INLINE int enable_except(int flags) {
  unsigned long value = FEnv::read();
  FEnv::write(value | (FEnv::to_bits(flags) << 8));
  return FEnv::to_flags(value >> 8);
}
LIBC_INLINE int disable_except(int flags) {
  unsigned long value = FEnv::read();
  FEnv::write(value & ~(FEnv::to_bits(flags) << 8));
  return FEnv::to_flags(value >> 8);
}
LIBC_INLINE int get_round() {
  switch ((FEnv::read() >> 16) & 3) {
  case 0:
    return FE_TONEAREST;
  case 1:
    return FE_TOWARDZERO;
  case 2:
    return FE_UPWARD;
  default:
    return FE_DOWNWARD;
  }
}
LIBC_INLINE int set_round(int mode) {
  unsigned long bits;
  switch (mode) {
  case FE_TONEAREST:
    bits = 0;
    break;
  case FE_TOWARDZERO:
    bits = 1;
    break;
  case FE_UPWARD:
    bits = 2;
    break;
  case FE_DOWNWARD:
    bits = 3;
    break;
  default:
    return -1;
  }
  FEnv::write((FEnv::read() & ~FEnv::ROUND_MASK) | (bits << 16));
  return 0;
}
LIBC_INLINE int get_env(fenv_t *env) {
  *env = FEnv::read() & FEnv::ENV_MASK;
  return 0;
}
LIBC_INLINE int set_env(const fenv_t *env) {
  unsigned long value = env == FE_DFL_ENV ? 0 : *env;
  FEnv::write((FEnv::read() & ~FEnv::ENV_MASK) | (value & FEnv::ENV_MASK));
  return 0;
}
LIBC_INLINE int raise_except(int flags) {
  // PUT only sets status. Execute real operations so enabled exceptions trip.
  unsigned long result;
  if (flags & FE_INVALID)
    LIBC_INLINE_ASM("FDIV %0,%1,%1" : "=r"(result) : "r"(0UL) : "memory");
  if (flags & FE_DIVBYZERO)
    LIBC_INLINE_ASM("FDIV %0,%1,%2" : "=r"(result) : "r"(0x3ff0000000000000UL),
                    "r"(0UL) : "memory");
  if (flags & FE_OVERFLOW)
    LIBC_INLINE_ASM(
        "FMUL %0,%1,%1" : "=r"(result) : "r"(0x7fefffffffffffffUL) : "memory");
  if (flags & FE_UNDERFLOW)
    LIBC_INLINE_ASM(
        "FMUL %0,%1,%1" : "=r"(result) : "r"(0x0010000000000000UL) : "memory");
  if (flags & FE_INEXACT)
    LIBC_INLINE_ASM("FDIV %0,%1,%2" : "=r"(result) : "r"(0x3ff0000000000000UL),
                    "r"(0x4008000000000000UL) : "memory");
  return 0;
}

} // namespace fputil
} // namespace LIBC_NAMESPACE_DECL
#endif
