//===-- Regional category admission --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/regional.h"
#include "hdr/regex_macros.h"
#include "src/locale/conventions.h"
#include "src/locale/time_category.h"
#include "src/regex/regcomp.h"
#include "src/regex/regfree.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

RegionalLocaleFiles::~RegionalLocaleFiles() {
  for (unsigned i = 0; i != count; ++i)
    files[i]->release();
}

ErrorOr<__locale_data *> RegionalLocaleFiles::load(unsigned category,
                                                   cpp::string_view name) {
  const char *canonical = locale_file_name(name);
  if (!canonical)
    return Error(ENOENT);
  for (unsigned i = 0; i != count; ++i)
    if (cpp::string_view(files[i]->view().name()) == canonical)
      return create_regional_category(*files[i], category);
  if (count == LOCALE_CATEGORY_COUNT)
    return Error(EINVAL);
  auto file = load_locale_file(canonical);
  if (!file)
    return Error(file.error());
  files[count++] = *file;
  return create_regional_category(**file, category);
}

ErrorOr<__locale_data *> create_regional_category(LocaleDataFile &file,
                                                  unsigned category) {
  if (category >= LOCALE_CATEGORY_COUNT)
    return Error(EINVAL);
  bool stateful =
      cpp::string_view(file.view().name()) == STATEFUL_VALIDATION_LOCALE;
  if (stateful && category != LC_CTYPE) {
    // This explicitly named validation profile inherits C's other categories;
    // it is not Chinese regional data. Its data is not installed by default.
    auto base = file.view().find(category, BUILTIN_CATEGORY_KEY);
    if (!base || base->kind != LocaleDataView::Kind::STRING ||
        base->bytes.size() != 2 || base->bytes[0] != 'C')
      return Error(EINVAL);
    return create_locale_category(file, false);
  }
  switch (category) {
  case LC_CTYPE: {
    auto transliteration = TransliterationData::read(file.view());
    if (!transliteration)
      return Error(transliteration.error());
    auto result = create_ctype_category(file);
    if (result && ((*result)->utf8 == stateful)) {
      release_locale_category(*result);
      return Error(EINVAL);
    }
    if (result)
      (*result)->transliteration = *transliteration;
    return result;
  }
  case LC_NUMERIC:
    return create_numeric_category(file);
  case LC_TIME:
    return create_time_category(file);
  case LC_COLLATE:
    return create_collation_category(file, true);
  case LC_MONETARY:
    return create_monetary_category(file);
  case LC_MESSAGES: {
    auto messages = MessagesLocaleData::read(file.view());
    if (!messages)
      return Error(messages.error());
    auto result = create_locale_category(file, true);
    if (result)
      (*result)->messages = *messages;
    return result;
  }
  default:
    return Error(EINVAL);
  }
}

int validate_locale_messages(__locale_t &locale) {
  auto *category = locale.data[LC_MESSAGES];
  if (!category->messages)
    return 0;
  struct Selection {
    locale_t previous;
    explicit Selection(__locale_t &candidate) : previous(thread_locale) {
      thread_locale = &candidate;
    }
    ~Selection() { thread_locale = previous; }
  } selection(locale);
  const auto &messages = *category->messages;
  const LocaleDataView::Item expressions[] = {messages.yesexpr,
                                              messages.noexpr};
  for (auto item : expressions) {
    regex_t compiled{};
    int error = LIBC_NAMESPACE::regcomp(
        &compiled, reinterpret_cast<const char *>(item.bytes.data()),
        REG_EXTENDED | REG_NOSUB);
    if (error)
      return error == REG_ESPACE ? ENOMEM : EINVAL;
    LIBC_NAMESPACE::regfree(&compiled);
  }
  return 0;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
