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
namespace {
struct OwnedLocale {
  __locale_t locale;
  char name[LOCALE_NAME_SIZE];
};
static_assert(offsetof(OwnedLocale, locale) == 0);
} // namespace

locale_t allocate_locale_object(const __locale_t &locale) {
  AllocChecker ac;
  auto *object = new (ac) OwnedLocale;
  if (!ac)
    return nullptr;
  update_locale_object(&object->locale, locale);
  return &object->locale;
}

void update_locale_object(locale_t object, const __locale_t &locale) {
  *object = locale;
  format_locale_name(locale, LC_ALL,
                     reinterpret_cast<OwnedLocale *>(object)->name);
}

const char *locale_object_name(locale_t object) {
  return reinterpret_cast<OwnedLocale *>(object)->name;
}

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

ErrorOr<__locale_data *> create_collation_category(LocaleDataFile &file,
                                                   bool utf8) {
  auto data = CollationData::read(file.view());
  if (!data)
    return Error(data.error());
  auto category = create_locale_category(file, utf8);
  if (category)
    (*category)->collation = *data;
  return category;
}

ErrorOr<__locale_data *> create_ctype_category(LocaleDataFile &file) {
  auto encoding = file.view().find(0, 0x200);
  if (!encoding || encoding->kind != LocaleDataView::Kind::STRING)
    return Error(EINVAL);
  cpp::string_view name(reinterpret_cast<const char *>(encoding->bytes.data()),
                        encoding->bytes.size() - 1);
  bool utf8 = name == "UTF-8";
  cpp::optional<ChineseEncodingData> mapping;
  if (!utf8) {
    if (name != "ISO-2022-CN")
      return Error(EINVAL);
    auto validated = ChineseEncodingData::read(file.view());
    if (!validated)
      return Error(validated.error());
    mapping = *validated;
  }
  auto data = CTypeLocaleData::read(file.view());
  if (!data)
    return Error(data.error());
  auto category = create_locale_category(file, utf8);
  if (category) {
    (*category)->ctype = *data;
    (*category)->chinese_encoding = mapping;
  }
  return category;
}

void release_locale_category(__locale_data *data) {
  if (data->storage &&
      data->references.fetch_sub(1, cpp::MemoryOrder::ACQ_REL) == 1) {
    ::free(data->grouping);
    ::free(data->currency);
    ::free(data->eras);
    ::free(data->alternative_digits);
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

bool publish_global_locale(const __locale_t &locale) {
  if (!retain_locale(locale))
    return false;
  release_locale(exchange_global_locale(locale));
  return true;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
