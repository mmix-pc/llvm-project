//===-- Implementation of locale ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/locale.h"
#include "src/__support/CPP/atomic.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

namespace internal {
static __locale_data c_data = {"C", false};
static __locale_data utf8_data = {"C.UTF-8", true};
// Publish all categories together without allocating or retaining old objects.
static cpp::Atomic<unsigned> global_mask{0};
LIBC_THREAD_LOCAL locale_t thread_locale = LC_GLOBAL_LOCALE;
const char *const category_names[LOCALE_CATEGORY_COUNT] = {
    "LC_CTYPE",   "LC_NUMERIC",  "LC_TIME",
    "LC_COLLATE", "LC_MONETARY", "LC_MESSAGES"};

__locale_t locale_from_mask(unsigned mask) {
  __locale_t result;
  for (int i = 0; i < LOCALE_CATEGORY_COUNT; ++i)
    result.data[i] = mask & (1U << i) ? &utf8_data : &c_data;
  return result;
}

unsigned locale_mask(const __locale_t &loc) {
  unsigned mask = 0;
  for (int i = 0; i < LOCALE_CATEGORY_COUNT; ++i)
    if (loc.data[i]->utf8)
      mask |= 1U << i;
  return mask;
}

__locale_t global_locale_snapshot() {
  return locale_from_mask(global_mask.load(cpp::MemoryOrder::RELAXED));
}

void publish_global_locale(const __locale_t &loc) {
  global_mask.store(locale_mask(loc), cpp::MemoryOrder::RELAXED);
}

__locale_t locale_snapshot(locale_t loc) {
  return loc == LC_GLOBAL_LOCALE ? global_locale_snapshot() : *loc;
}

bool locale_is_utf8() {
  return locale_snapshot(thread_locale).data[LC_CTYPE]->utf8;
}
} // namespace internal

} // namespace LIBC_NAMESPACE_DECL
