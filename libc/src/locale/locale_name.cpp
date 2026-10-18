//===-- Built-in locale names and environment selection -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/CPP/string_view.h"
#include "src/locale/locale.h"
#include "src/stdlib/environ_internal.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
static int builtin(cpp::string_view name) {
  if (name == "C" || name == "POSIX")
    return 0;
  if (name == "C.UTF-8" || name == "C.utf8")
    return 1;
  return -1;
}

bool resolve_locale(int mask, const char *name, __locale_t &result) {
  cpp::string_view input(name);
  __locale_t candidate = result;
  // The LC_ALL query form is accepted only as a complete, ordered list.
  if (input.substr(0, 9) == "LC_CTYPE=") {
    if (mask != LOCALE_MASK)
      return false;
    for (int i = 0; i < LOCALE_CATEGORY_COUNT; ++i) {
      cpp::string_view key(category_names[i]);
      if (input.substr(0, key.size()) != key || input.size() <= key.size() ||
          input[key.size()] != '=')
        return false;
      input.remove_prefix(key.size() + 1);
      size_t end = input.find_first_of(';');
      if ((i == LOCALE_CATEGORY_COUNT - 1) != (end == cpp::string_view::npos))
        return false;
      int value = builtin(input.substr(0, end));
      if (value < 0)
        return false;
      candidate.data[i] = locale_from_mask(value ? LOCALE_MASK : 0).data[i];
      if (end != cpp::string_view::npos)
        input.remove_prefix(end + 1);
    }
  } else {
    for (int i = 0; i < LOCALE_CATEGORY_COUNT; ++i) {
      if (!(mask & (1 << i)))
        continue;
      const char *selected = name;
      if (input.empty()) {
        selected = EnvironmentManager::get_instance().get("LC_ALL");
        if (!selected || !*selected)
          selected = EnvironmentManager::get_instance().get(category_names[i]);
        if (!selected || !*selected)
          selected = EnvironmentManager::get_instance().get("LANG");
        if (!selected || !*selected)
          selected = "C";
      }
      int value = builtin(selected);
      if (value < 0)
        return false;
      candidate.data[i] = locale_from_mask(value ? LOCALE_MASK : 0).data[i];
    }
  }
  result = candidate;
  return true;
}

char *locale_name(const __locale_t &loc, int category) {
  static LIBC_THREAD_LOCAL char buffer[192];
  char *out = buffer;
  auto append = [&](const char *s) {
    while (*s)
      *out++ = *s++;
  };
  unsigned mask = locale_mask(loc);
  if (category != LC_ALL || mask == 0 || mask == LOCALE_MASK) {
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
  return buffer;
}
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
