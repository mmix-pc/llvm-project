//===-- Linux public message selection ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/libintl/message_lookup.h"
#include "src/__support/OSUtil/linux/secure_execution.h"
#include "src/__support/libc_errno.h"
#include "src/__support/wchar/encoding_converter.h"
#include "src/libintl/domain_bindings.h"
#include "src/libintl/message_file.h"
#include "src/libintl/message_path.h"
#include "src/libintl/message_results.h"
#include "src/libintl/message_translation.h"
#include "src/locale/builtin_transliteration.h"
#include "src/locale/locale.h"
#include "src/locale/locale_file.h"
#include "src/stdlib/environ_internal.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
MessageResults results;
char *lookup(const char *domain, const char *singular, const char *plural,
             cpp::optional<unsigned long> count, int category, locale_t locale,
             bool diagnostic) {
  // Include all allocations, resource disposal and lock operations, not just
  // syscalls: even fallback must preserve the caller's errno.
  struct Errno {
    int saved = libc_errno;
    ~Errno() { libc_errno = saved; }
  } errno_guard;
  char *fallback = const_cast<char *>(count && *count != 1 ? plural : singular);
  if (!singular || category < 0 || category >= LOCALE_CATEGORY_COUNT)
    return fallback;
  auto selected = locale_snapshot(locale ? locale : thread_locale);
  const char *name = selected.__data[category]->name;
  if (cpp::string_view(name) == "C" || cpp::string_view(name) == "POSIX")
    return fallback;

  DomainSnapshot binding;
  if (!snapshot_text_domain(domain, binding))
    return fallback;
  bool environment = !is_secure_execution();
  auto &env = EnvironmentManager::get_instance();
  auto found = search_message_object(
      binding.directory(), name, category_names[category], binding.domain(),
      environment ? env.get("NLSPATH") : nullptr,
      environment ? env.get("LANGUAGE") : nullptr,
      [](cpp::string_view path) { return load_message_file(path.data()); });
  if (!found)
    return fallback;
  struct Catalog {
    MoCatalog *value;
    ~Catalog() { value->release(); }
  } catalog{*found};
  const auto *ctype = selected.__data[LC_CTYPE];
  const char *output = diagnostic ? nullptr : binding.codeset();
  if (!output)
    output = ctype->chinese_encoding ? "ISO-2022-CN"
             : ctype->utf8           ? "UTF-8"
                                     : "ISO-8859-1";
  struct Mapping {
    LocaleDataFile *value = nullptr;
    ~Mapping() {
      if (value)
        value->release();
    }
  } mapping;
  cpp::optional<TransliterationData> builtin;
  const TransliterationData *rules = nullptr;
  auto charset = catalog.value->view().charset();
  if (charset) {
    auto names = EncodingConverter::parse_names(*charset, output);
    if (!names)
      return fallback;
    if (names->needs_mapping()) {
      auto loaded = load_chinese_encoding_file();
      if (!loaded)
        return fallback;
      mapping.value = *loaded;
    }
    if (names->policy == EncodingConverter::NonIdentical::TRANSLIT) {
      if (ctype->transliteration) {
        rules = &*ctype->transliteration;
      } else {
        auto loaded = builtin_transliteration();
        if (!loaded)
          return fallback;
        builtin = cpp::optional<TransliterationData>(*loaded);
        rules = &*builtin;
      }
    }
  } else if (charset.error() != ENOENT) {
    return fallback;
  }
  size_t size;
  auto translated = translate_message(catalog.value->view(), singular, count,
                                      output, mapping.value, rules, &size);
  if (!translated)
    return fallback;
  auto stored = results.insert(*translated, size);
  return stored ? *stored : fallback;
}
} // namespace

bool try_lock_message_results_for_fork() { return results.try_lock_for_fork(); }

void unlock_message_results_after_fork() { results.unlock_after_fork(); }

char *lookup_message(const char *domain, const char *singular,
                     const char *plural, cpp::optional<unsigned long> count,
                     int category, locale_t locale) {
  return lookup(domain, singular, plural, count, category, locale, false);
}

const char *lookup_diagnostic_message(const char *message, locale_t locale) {
  return lookup("llvm-libc", message, nullptr, cpp::nullopt, LC_MESSAGES,
                locale, true);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
