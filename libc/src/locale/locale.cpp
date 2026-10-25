//===-- Implementation of locale ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/locale.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

namespace internal {
static __locale_data c_data = {"C", false};
static __locale_data utf8_data = {"C.UTF-8", true};
// Applications synchronize global locale changes with affected operations.
static __locale_t global_locale = {
    {&c_data, &c_data, &c_data, &c_data, &c_data, &c_data}};
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
  return global_locale;
}

__locale_t exchange_global_locale(const __locale_t &loc) {
  auto previous = global_locale;
  global_locale = loc;
  return previous;
}

__locale_t locale_snapshot(locale_t loc) {
  return loc == LC_GLOBAL_LOCALE ? global_locale_snapshot() : *loc;
}

bool locale_is_utf8() {
  return locale_snapshot(thread_locale).data[LC_CTYPE]->utf8;
}

const ChineseEncodingData *locale_chinese_encoding() {
  auto *category = locale_snapshot(thread_locale).data[LC_CTYPE];
  return category->chinese_encoding ? &*category->chinese_encoding : nullptr;
}

void format_locale_name(const __locale_t &loc, int category,
                        char (&buffer)[LOCALE_NAME_SIZE]) {
  char *out = buffer;
  auto append = [&](const char *s) {
    while (*s)
      *out++ = *s++;
  };
  bool same_name = true;
  for (int i = 1; i < LOCALE_CATEGORY_COUNT; ++i)
    same_name &= cpp::string_view(loc.data[i]->name) == loc.data[0]->name;
  if (category != LC_ALL || same_name) {
    append(loc.data[category == LC_ALL ? LC_CTYPE : category]->name);
  } else {
    for (int i = 0; i < LOCALE_CATEGORY_COUNT; ++i) {
      if (i)
        *out++ = ';';
      append(category_names[i]);
      *out++ = '=';
      append(loc.data[i]->name);
    }
  }
  *out = '\0';
}

char *locale_name(const __locale_t &loc, int category) {
  static LIBC_THREAD_LOCAL char buffer[LOCALE_NAME_SIZE];
  format_locale_name(loc, category, buffer);
  return buffer;
}
} // namespace internal

} // namespace LIBC_NAMESPACE_DECL
