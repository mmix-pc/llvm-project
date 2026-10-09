//===-- MMIX Linux concurrency hints --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "concurrency_hint.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/atomic.h"
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {
// Reserve the whole octa used by MMIX subword atomic updates.
struct alignas(8) Hint {
  cpp::Atomic<int> value{0};
  unsigned reserved = 0;
};
static_assert(sizeof(Hint) == 8);
LIBC_CONSTINIT Hint hint;
} // namespace
int get_concurrency_hint() {
  return hint.value.load(cpp::MemoryOrder::RELAXED);
}
int set_concurrency_hint(int value) {
  if (value < 0)
    return EINVAL;
  hint.value.store(value, cpp::MemoryOrder::RELAXED);
  return 0;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
