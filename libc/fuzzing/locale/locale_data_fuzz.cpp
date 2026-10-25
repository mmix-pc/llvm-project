//===-- Locale container validation fuzzing -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/collation_data.h"
#include "src/locale/collation_key.h"
#include "src/locale/ctype_data.h"
#include "src/locale/encoding_data.h"
#include "src/locale/locale_data.h"
#include "src/locale/messages_data.h"
#include "src/locale/monetary_data.h"
#include "src/locale/numeric_data.h"
#include "src/locale/time_data.h"
#include "src/locale/transliteration_data.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  using LIBC_NAMESPACE::internal::LocaleDataFile;
  using LIBC_NAMESPACE::internal::LocaleDataView;
  auto view = LocaleDataView::parse({data, size});
  if (!view)
    return 0;
  (void)LIBC_NAMESPACE::internal::NumericLocaleData::read(*view);
  (void)LIBC_NAMESPACE::internal::MessagesLocaleData::read(*view);
  auto collation = LIBC_NAMESPACE::internal::CollationData::read(*view);
  if (collation) {
    uint32_t source[] = {uint32_t(size)};
    auto rule = collation->lookup(source);
    if (rule) {
      (void)rule->direction(size);
      auto weights = rule->weights(size % 4);
      if (weights)
        (void)weights->at(size);
      uint8_t key[16];
      (void)LIBC_NAMESPACE::internal::transform_collation({&*rule, 1}, key);
    }
  }
  auto transliteration =
      LIBC_NAMESPACE::internal::TransliterationData::read(*view);
  if (transliteration && transliteration->size()) {
    auto rule = transliteration->rule(0);
    (void)rule->alternative(size);
    uint32_t source[] = {uint32_t(size)};
    (void)transliteration->find(source);
  }
  auto encoding = LIBC_NAMESPACE::internal::ChineseEncodingData::read(*view);
  if (encoding) {
    (void)encoding->decode(LIBC_NAMESPACE::internal::ChineseEncodingData::CNS1,
                           uint32_t(size));
    (void)encoding->encode(
        LIBC_NAMESPACE::internal::ChineseEncodingData::GB2312, uint32_t(size));
  }
  (void)LIBC_NAMESPACE::internal::MonetaryLocaleData::read(*view);
  auto time = LIBC_NAMESPACE::internal::TimeLocaleData::read(*view);
  if (time) {
    (void)time->text(LIBC_NAMESPACE::internal::TimeLocaleData::MON, size);
    (void)time->count(LIBC_NAMESPACE::internal::TimeLocaleData::ERAS);
  }
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
