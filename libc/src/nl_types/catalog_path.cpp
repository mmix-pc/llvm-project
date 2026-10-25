//===-- Numbered catalog path expansion -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/nl_types/catalog_path.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

ErrorOr<size_t> expand_catalog_path(cpp::string_view pattern,
                                    cpp::string_view name,
                                    cpp::string_view locale,
                                    cpp::span<char> output) {
  if (pattern.empty())
    pattern = "%N";
  size_t used = 0;
  auto append = [&](cpp::string_view value) {
    if (output.empty() || value.size() >= output.size() - used)
      return false;
    for (char c : value)
      output[used++] = c;
    return true;
  };
  auto base = locale.substr(0, locale.find_first_of('@'));
  auto territory = cpp::string_view();
  auto codeset = cpp::string_view();
  size_t dot = base.find_first_of('.');
  size_t underscore = base.find_first_of('_');
  auto language = base.substr(0, dot < underscore ? dot : underscore);
  if (dot != cpp::string_view::npos)
    codeset = base.substr(dot + 1);
  if (underscore != cpp::string_view::npos &&
      (dot == cpp::string_view::npos || underscore < dot))
    territory = base.substr(underscore + 1, dot == cpp::string_view::npos
                                                ? cpp::string_view::npos
                                                : dot - underscore - 1);
  for (size_t i = 0; i != pattern.size(); ++i) {
    cpp::string_view value = pattern.substr(i, 1);
    if (pattern[i] == '%') {
      if (++i == pattern.size())
        return Error(EINVAL);
      switch (pattern[i]) {
      case 'N':
        value = name;
        break;
      case 'L':
        value = locale;
        break;
      case 'l':
        value = language;
        break;
      case 't':
        value = territory;
        break;
      case 'c':
        value = codeset;
        break;
      case '%':
        value = "%";
        break;
      default:
        return Error(EINVAL);
      }
    }
    if (!append(value))
      return Error(ENAMETOOLONG);
  }
  output[used] = 0;
  return used;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
