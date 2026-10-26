//===-- Translation catalog path construction -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/libintl/message_path.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
ErrorOr<size_t> message_object_path(cpp::string_view directory,
                                    cpp::string_view locale,
                                    cpp::string_view category,
                                    cpp::string_view domain,
                                    cpp::span<char> output) {
  if (directory.empty() || locale.empty() || category.empty() || domain.empty())
    return Error(EINVAL);
  while (!directory.empty() && directory.back() == '/')
    directory.remove_suffix(1);
  size_t used = 0;
  const cpp::string_view pieces[] = {directory, "/", locale, "/",
                                     category,  "/", domain, ".mo"};
  for (auto piece : pieces) {
    if (output.empty() || piece.size() >= output.size() - used)
      return Error(ENAMETOOLONG);
    for (char c : piece)
      output[used++] = c;
  }
  output[used] = 0;
  return used;
}
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
