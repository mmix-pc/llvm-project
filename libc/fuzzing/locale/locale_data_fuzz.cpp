//===-- Locale container validation fuzzing -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/ctype_data.h"
#include "src/locale/locale_data.h"
#include "src/locale/monetary_data.h"
#include "src/locale/numeric_data.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  using LIBC_NAMESPACE::internal::LocaleDataFile;
  using LIBC_NAMESPACE::internal::LocaleDataView;
  auto view = LocaleDataView::parse({data, size});
  if (!view)
    return 0;
  (void)LIBC_NAMESPACE::internal::NumericLocaleData::read(*view);
  (void)LIBC_NAMESPACE::internal::MonetaryLocaleData::read(*view);
  auto characters = LIBC_NAMESPACE::internal::CTypeLocaleData::read(*view);
  if (characters) {
    (void)characters->contains(LIBC_NAMESPACE::internal::CTypeLocaleData::ALPHA,
                               uint32_t(size));
    (void)characters->map(LIBC_NAMESPACE::internal::CTypeLocaleData::TOUPPER,
                          uint32_t(size));
  }
  const uint32_t keys[] = {1, 10, 20, 30, 0xffffffff};
  for (unsigned category = 0; category != 6; ++category) {
    for (uint32_t key : keys) {
      auto item = view->find(category, key);
      if (item && item->kind == LocaleDataView::Kind::WORDS)
        (void)item->word(item->bytes.size() / 4);
    }
  }
  auto copy = LocaleDataFile::copy({data, size});
  if (copy) {
    LocaleDataFile *file = copy.value();
    if (file->retain())
      file->release();
    file->release();
  }
  return 0;
}
