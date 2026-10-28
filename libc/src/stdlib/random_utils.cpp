//===-- BSD additive-feedback random state --------------------------------===//
//
// Derived from NetBSD random.c,v 1.5 (2016-02-08), as preserved in Bionic
// a9750eda0/libc/upstream-netbsd/common/lib/libc/stdlib/random.c.
//
// Copyright (c) 1983, 1993
// The Regents of the University of California. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. Neither the name of the University nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
// OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
// SUCH DAMAGE.
//
// SPDX-License-Identifier: BSD-3-Clause
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/random_utils.h"
#include "hdr/errno_macros.h"
#include "hdr/stdint_proxy.h"
#include "src/__support/CPP/mutex.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/mutex.h"
#include "src/string/memory_utils/inline_memcpy.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
constexpr unsigned DEGREES[] = {0, 7, 15, 31, 63};
constexpr unsigned SEPARATIONS[] = {0, 3, 1, 3, 1};
constexpr size_t SIZES[] = {8, 32, 64, 128, 256};
// The source's precomputed seed-one state after 310 warmup steps.
uint32_t default_state[] = {
    3,          0x991539b1, 0x16a5bce3, 0x6774a4cd, 0x3e01511e, 0x4e508aaa,
    0x61048c05, 0xf5500617, 0x846b7115, 0x6a19892c, 0x896a97af, 0xdb48f936,
    0x14898454, 0x37ffd106, 0xb58bff9c, 0x59e17104, 0xcf918a49, 0x09378c83,
    0x52c7a471, 0x8d293ea9, 0x1f4fc301, 0xc3db71be, 0x39b44e1c, 0xf8a44ef9,
    0x4c8b80b1, 0x19edc328, 0x87bf4bdd, 0xc9b240e5, 0xe9ee4b1b, 0x4382aee7,
    0x535b6b41, 0xf3bec5da};
Mutex state_mutex(false, false, false, false);
char *state = reinterpret_cast<char *>(default_state);
unsigned type = 3, rear = 0;

uint32_t load(const char *buffer, unsigned index) {
  uint32_t value;
  inline_memcpy(&value, buffer + sizeof(value) * index, sizeof(value));
  return value;
}
void store(char *buffer, unsigned index, uint32_t value) {
  inline_memcpy(buffer + sizeof(value) * index, &value, sizeof(value));
}
void save_position() { store(state, 0, 5 * rear + type); }

long next_unlocked() {
  if (!type) {
    uint32_t value =
        (load(state, 1) * uint32_t(1103515245) + 12345) & 0x7fffffff;
    store(state, 1, value);
    return value;
  }
  unsigned front = (rear + SEPARATIONS[type]) % DEGREES[type];
  uint32_t value = load(state, front + 1) + load(state, rear + 1);
  store(state, front + 1, value);
  rear = (rear + 1) % DEGREES[type];
  return value >> 1;
}

void seed_unlocked(unsigned seed) {
  store(state, 1, seed);
  if (type) {
    for (unsigned i = 1; i < DEGREES[type]; ++i) {
      uint32_t bits = load(state, i);
      // Preserve BSD's signed seed interpretation without signed overflow.
      int64_t previous = bits < 0x80000000U
                             ? int64_t(bits)
                             : int64_t(bits) - (int64_t(1) << 32);
      int64_t value = 16807 * (previous % 127773) - 2836 * (previous / 127773);
      if (value <= 0)
        value += 0x7fffffff;
      store(state, i + 1, uint32_t(value));
    }
  }
  rear = 0;
  for (unsigned i = 0; i < 10 * DEGREES[type]; ++i)
    (void)next_unlocked();
  save_position();
}
} // namespace

long random_next() {
  cpp::lock_guard guard(state_mutex);
  return next_unlocked();
}

void random_seed(unsigned seed) {
  cpp::lock_guard guard(state_mutex);
  seed_unlocked(seed);
}

char *random_initialize(unsigned seed, char *buffer, size_t size) {
  if (size < SIZES[0] || !buffer) {
    libc_errno = EINVAL;
    return nullptr;
  }
  unsigned selected = 0;
  while (selected < 4 && size >= SIZES[selected + 1])
    ++selected;
  cpp::lock_guard guard(state_mutex);
  char *previous = state;
  save_position();
  state = buffer;
  type = selected;
  seed_unlocked(seed);
  return previous;
}

char *random_set_state(char *buffer) {
  cpp::lock_guard guard(state_mutex);
  if (buffer == state) {
    save_position();
    return state;
  }
  if (!buffer) {
    libc_errno = EINVAL;
    return nullptr;
  }
  uint32_t metadata = load(buffer, 0);
  unsigned selected = metadata % 5, position = metadata / 5;
  if (position >= (selected ? DEGREES[selected] : 1)) {
    libc_errno = EINVAL;
    return nullptr;
  }
  char *previous = state;
  save_position();
  state = buffer;
  type = selected;
  rear = position;
  return previous;
}

bool try_lock_random_for_fork() {
  return state_mutex.try_lock() == MutexError::NONE;
}
void unlock_random_after_fork() {
  if (state_mutex.unlock() != MutexError::NONE)
    __builtin_trap();
}
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
