//===-- Transactional locale names and environment selection --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/CPP/string_view.h"
#include "src/locale/locale.h"
#include "src/locale/locale_storage.h"
#ifdef LIBC_LOCALE_HAS_REGIONAL_DATA
#include "src/locale/regional.h"
#endif
#ifndef LIBC_TARGET_OS_IS_BAREMETAL
#include "src/stdlib/environ_internal.h"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace internal {
static int builtin(cpp::string_view name) {
  if (name == "C" || name == "POSIX")
    return 0;
  if (name == "C.UTF-8" || name == "C.utf8")
    return 1;
  return -1;
}

ErrorOr<__locale_t> resolve_locale(int mask, const char *name,
                                   const __locale_t &base) {
  cpp::string_view input(name);
  cpp::string_view names[LOCALE_CATEGORY_COUNT];
  // The LC_ALL query form is accepted only as a complete, ordered list.
  if (input.substr(0, 9) == "LC_CTYPE=") {
    if (mask != LOCALE_MASK)
      return Error(ENOENT);
    for (int i = 0; i < LOCALE_CATEGORY_COUNT; ++i) {
      cpp::string_view key(category_names[i]);
      if (input.substr(0, key.size()) != key || input.size() <= key.size() ||
          input[key.size()] != '=')
        return Error(ENOENT);
      input.remove_prefix(key.size() + 1);
      size_t end = input.find_first_of(';');
      if ((i == LOCALE_CATEGORY_COUNT - 1) != (end == cpp::string_view::npos))
        return Error(ENOENT);
      names[i] = input.substr(0, end);
      if (end != cpp::string_view::npos)
        input.remove_prefix(end + 1);
    }
  } else {
    for (int i = 0; i < LOCALE_CATEGORY_COUNT; ++i) {
      if (!(mask & (1 << i)))
        continue;
      const char *selected = name;
      if (input.empty()) {
#ifndef LIBC_TARGET_OS_IS_BAREMETAL
        selected = EnvironmentManager::get_instance().get("LC_ALL");
        if (!selected || !*selected)
          selected = EnvironmentManager::get_instance().get(category_names[i]);
        if (!selected || !*selected)
          selected = EnvironmentManager::get_instance().get("LANG");
#endif
        // Bare-metal startup has no process environment to select a locale.
        if (!selected || !*selected)
          selected = "C";
      }
      names[i] = selected;
    }
  }
  __locale_t candidate = base;
#ifdef LIBC_LOCALE_HAS_REGIONAL_DATA
  RegionalLocaleFiles files;
#endif
  for (int i = 0; i != LOCALE_CATEGORY_COUNT; ++i) {
    int error = 0;
    if (!(mask & (1 << i))) {
      if (!retain_locale_category(candidate.__data[i]))
        error = ENOMEM;
    } else if (int value = builtin(names[i]); value >= 0) {
      candidate.__data[i] = locale_from_mask(value ? LOCALE_MASK : 0).__data[i];
    } else {
#ifdef LIBC_LOCALE_HAS_REGIONAL_DATA
      auto category = files.load(i, names[i]);
      if (category)
        candidate.__data[i] = *category;
      else
        error = category.error();
#else
      error = ENOENT;
#endif
    }
    if (error) {
      while (i)
        release_locale_category(candidate.__data[--i]);
      return Error(error);
    }
  }
#ifdef LIBC_LOCALE_HAS_REGIONAL_DATA
  if (int error = validate_locale_messages(candidate)) {
    release_locale(candidate);
    return Error(error);
  }
#endif
  return candidate;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
