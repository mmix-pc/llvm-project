//===-- Calendar category selection --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/time_locale.h"
#ifdef LIBC_FULL_BUILD
#include "src/locale/locale.h"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace internal {
const TimeLocaleData *time_locale(locale_t locale) {
#ifdef LIBC_FULL_BUILD
  auto snapshot = locale_snapshot(locale);
  const auto &data = snapshot.data[LC_TIME]->time;
  return data ? &*data : nullptr;
#else
  // Overlay builds cannot interpret another libc's opaque locale objects.
  (void)locale;
  return nullptr;
#endif
}
const TimeLocaleData *current_time_locale() {
#ifdef LIBC_FULL_BUILD
  return time_locale(thread_locale);
#else
  return nullptr;
#endif
}
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
