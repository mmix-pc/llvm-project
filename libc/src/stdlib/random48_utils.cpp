//===-- State for the 48-bit random generator ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/random48_utils.h"
#include "src/__support/CPP/mutex.h"
#include "src/__support/threads/mutex.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
constexpr uint64_t MASK = (uint64_t(1) << 48) - 1;
constexpr uint64_t MULTIPLIER = 0x5deece66d;
constexpr unsigned short ADDEND = 0xb;
Mutex state_mutex(false, false, false, false);
uint64_t current_state = 0;
uint64_t multiplier = MULTIPLIER;
unsigned short addend = ADDEND;
unsigned short previous_state[3]{};

uint64_t load(const unsigned short state[3]) {
  return uint64_t(state[0]) | (uint64_t(state[1]) << 16) |
         (uint64_t(state[2]) << 32);
}

void store(unsigned short state[3], uint64_t value) {
  for (unsigned i = 0; i < 3; ++i)
    state[i] = static_cast<unsigned short>(value >> (16 * i));
}

uint64_t advance(uint64_t value, uint64_t a, unsigned short c) {
  // Unsigned wraparound preserves the low 48 bits of the product.
  return (value * a + c) & MASK;
}
} // namespace

uint64_t random48_next() {
  cpp::lock_guard guard(state_mutex);
  current_state = advance(current_state, multiplier, addend);
  return current_state;
}

uint64_t random48_next(unsigned short state[3]) {
  uint64_t a;
  unsigned short c;
  {
    cpp::lock_guard guard(state_mutex);
    a = multiplier;
    c = addend;
  }
  uint64_t value = advance(load(state), a, c);
  store(state, value);
  return value;
}

void random48_seed(long seed) {
  cpp::lock_guard guard(state_mutex);
  current_state = (uint64_t(uint32_t(seed)) << 16) | 0x330e;
  multiplier = MULTIPLIER;
  addend = ADDEND;
}

unsigned short *random48_seed(unsigned short seed[3]) {
  cpp::lock_guard guard(state_mutex);
  // seed may point to the storage returned by the previous seed48 call.
  uint64_t replacement = load(seed);
  store(previous_state, current_state);
  current_state = replacement;
  multiplier = MULTIPLIER;
  addend = ADDEND;
  return previous_state;
}

void random48_parameters(unsigned short parameters[7]) {
  cpp::lock_guard guard(state_mutex);
  current_state = load(parameters);
  multiplier = load(parameters + 3);
  addend = parameters[6];
}

bool try_lock_random48_for_fork() {
  return state_mutex.try_lock() == MutexError::NONE;
}

void unlock_random48_after_fork() {
  if (state_mutex.unlock() != MutexError::NONE)
    __builtin_trap();
}
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
