//===-- Translation catalog search ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LIBINTL_MESSAGE_PATH_H
#define LLVM_LIBC_SRC_LIBINTL_MESSAGE_PATH_H

#include "src/libintl/mo_catalog.h"
#include "src/nl_types/catalog_path.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

ErrorOr<size_t> message_object_path(cpp::string_view directory,
                                    cpp::string_view locale,
                                    cpp::string_view category,
                                    cpp::string_view domain,
                                    cpp::span<char> output);

// Environment policy is supplied by the caller; privileged callers pass no
// environment search paths. The first valid object wins, even when a later
// translation lookup misses. No implicit locale-name variants are invented.
template <typename Open>
ErrorOr<MoCatalog *>
search_message_object(cpp::string_view directory, cpp::string_view locale,
                      cpp::string_view category, cpp::string_view domain,
                      const char *nlspath, const char *language, Open open) {
  if (locale == "C" || locale == "POSIX")
    return Error(ENOENT);
  auto terminal = [](int error) {
    return error == ENOMEM || error == EMFILE || error == ENFILE ||
           error == EINTR;
  };
  char path[4096];
  if (nlspath && *nlspath) {
    cpp::string_view remaining(nlspath);
    for (;;) {
      size_t end = remaining.find_first_of(':');
      auto size = expand_catalog_path(remaining.substr(0, end), domain, locale,
                                      {path, sizeof(path)});
      if (size) {
        auto found = open(cpp::string_view(path, *size));
        if (found || terminal(found.error()))
          return found;
      }
      if (end == cpp::string_view::npos)
        break;
      remaining.remove_prefix(end + 1);
    }
  }
  if (language && *language) {
    cpp::string_view remaining(language);
    for (;;) {
      size_t end = remaining.find_first_of(':');
      auto name = remaining.substr(0, end);
      // LANGUAGE is untrusted path input, not an explicit application path.
      if (!name.empty() && name != "." && name != ".." &&
          name.find_first_of('/') == cpp::string_view::npos) {
        auto size = message_object_path(directory, name, category, domain,
                                        {path, sizeof(path)});
        if (size) {
          auto found = open(cpp::string_view(path, *size));
          if (found || terminal(found.error()))
            return found;
        }
      }
      if (end == cpp::string_view::npos)
        break;
      remaining.remove_prefix(end + 1);
    }
  }
  auto size = message_object_path(directory, locale, category, domain,
                                  {path, sizeof(path)});
  if (!size)
    return Error(size.error());
  return open(cpp::string_view(path, *size));
}
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
