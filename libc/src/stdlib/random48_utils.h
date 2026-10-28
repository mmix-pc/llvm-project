//===-- Internal 48-bit random generator ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_STDLIB_RANDOM48_UTILS_H
#define LLVM_LIBC_SRC_STDLIB_RANDOM48_UTILS_H

#include "hdr/stdint_proxy.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
uint64_t random48_next();
uint64_t random48_next(unsigned short state[3]);
void random48_seed(long seed);
unsigned short *random48_seed(unsigned short seed[3]);
void random48_parameters(unsigned short parameters[7]);
bool try_lock_random48_for_fork();
void unlock_random48_after_fork();

inline long random48_signed(uint64_t value) {
  uint32_t upper = static_cast<uint32_t>(value >> 16);
  return upper <= 0x7fffffffU ? static_cast<long>(upper)
                              : -1L - static_cast<long>(uint32_t(~upper));
}
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
