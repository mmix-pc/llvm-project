//===-- Numbered catalog search ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_NL_TYPES_CATALOG_PATH_H
#define LLVM_LIBC_SRC_NL_TYPES_CATALOG_PATH_H

#include "hdr/errno_macros.h"
#include "src/__support/CPP/string_view.h"
#include "src/nl_types/catalog_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

ErrorOr<size_t> expand_catalog_path(cpp::string_view pattern,
                                    cpp::string_view name,
                                    cpp::string_view locale,
                                    cpp::span<char> output);

// Empty components mean %N. A bad candidate does not terminate the search;
// resource exhaustion and interruption must not be hidden by later misses.
template <typename Open>
ErrorOr<MessageCatalog *>
search_catalog(cpp::string_view name, cpp::string_view locale,
               const char *paths, cpp::string_view defaults, Open open) {
  if (name.empty())
    return Error(ENOENT);
  if (name.find_first_of('/') != cpp::string_view::npos)
    return open(name);
  int error = ENOENT;
  for (unsigned phase = 0; phase != 2; ++phase) {
    if (!phase && !paths)
      continue;
    cpp::string_view remaining = phase ? defaults : cpp::string_view(paths);
    for (;;) {
      size_t end = remaining.find_first_of(':');
      char buffer[4096];
      auto size = expand_catalog_path(remaining.substr(0, end), name, locale,
                                      {buffer, sizeof(buffer)});
      if (size) {
        auto result = open(cpp::string_view(buffer, *size));
        if (result)
          return result;
        error = result.error();
        if (error == ENOMEM || error == EMFILE || error == ENFILE ||
            error == EINTR)
          return Error(error);
      } else {
        error = size.error();
      }
      if (end == cpp::string_view::npos)
        break;
      remaining.remove_prefix(end + 1);
    }
  }
  return Error(error);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_SRC_NL_TYPES_CATALOG_PATH_H
