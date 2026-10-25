//===-- Locale category ownership -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/locale_storage.h"
#include "hdr/errno_macros.h"
#include "hdr/func/free.h"
#include "src/__support/alloc-checker.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

ErrorOr<__locale_data *> create_locale_category(LocaleDataFile &file,
                                                bool utf8) {
  AllocChecker ac;
  auto *category = new (ac) __locale_data(file.view().name(), utf8, &file);
  if (!ac)
    return Error(ENOMEM);
  if (!file.retain()) {
    category->~__locale_data();
    ::free(category);
    return Error(ENOMEM);
  }
  return category;
}

bool retain_locale_category(__locale_data *data) {
  if (!data->storage)
    return true;
  unsigned old = data->references.load(cpp::MemoryOrder::RELAXED);
  do {
    if (old == ~0U)
      return false;
  } while (!data->references.compare_exchange_weak(old, old + 1,
                                                   cpp::MemoryOrder::RELAXED));
  return true;
}

void release_locale_category(__locale_data *data) {
  if (data->storage &&
      data->references.fetch_sub(1, cpp::MemoryOrder::ACQ_REL) == 1) {
    data->storage->release();
    data->~__locale_data();
    ::free(data);
  }
}

bool retain_locale(const __locale_t &locale) {
  for (int i = 0; i != LOCALE_CATEGORY_COUNT; ++i) {
    if (!retain_locale_category(locale.data[i])) {
      while (i)
        release_locale_category(locale.data[--i]);
      return false;
    }
  }
  return true;
}

void release_locale(const __locale_t &locale) {
  for (auto *category : locale.data)
    release_locale_category(category);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
