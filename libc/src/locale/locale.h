//===-- Implementation header for the locale --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_LOCALE_H
#define LLVM_LIBC_SRC_LOCALE_LOCALE_H

#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"

#include "hdr/locale_macros.h"
#include "hdr/types/locale_t.h"

#include <stddef.h>

// Immutable built-in category records; objects own only their pointer array.
struct __locale_data {
  const char *name;
  bool utf8;
};

namespace LIBC_NAMESPACE_DECL {
namespace internal {
inline constexpr int LOCALE_CATEGORY_COUNT = 6;
inline constexpr int LOCALE_MASK = (1 << LOCALE_CATEGORY_COUNT) - 1;
// Thread selection borrows an object. The caller must keep it alive and must
// not modify it while any thread uses it. The global sentinel is not an object.
extern LIBC_THREAD_LOCAL locale_t thread_locale;
extern const char *const category_names[LOCALE_CATEGORY_COUNT];
__locale_t locale_from_mask(unsigned mask);
unsigned locale_mask(const __locale_t &loc);
__locale_t global_locale_snapshot();
void publish_global_locale(const __locale_t &loc);
__locale_t locale_snapshot(locale_t loc);
bool locale_is_utf8();
// Resolution is transactional. Environment mutation and concurrent setlocale
// writers require caller synchronization; readers see complete global snapshots.
bool resolve_locale(int mask, const char *name, __locale_t &result);
// The returned thread-local buffer is reused by the next setlocale call.
char *locale_name(const __locale_t &loc, int category);
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_LOCALE_LOCALE_H
