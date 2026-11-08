//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the implementation of the fnmatch function.
///
//===----------------------------------------------------------------------===//

#include "src/fnmatch/fnmatch.h"
#include "hdr/fnmatch_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/fnmatch/matcher.h"
#ifdef LIBC_FULL_BUILD
#include "src/locale/locale.h"
#endif

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, fnmatch,
                   (const char *pattern, const char *string, int flags)) {
  const internal::CollationData *collation = nullptr;
#ifdef LIBC_FULL_BUILD
  const auto *category =
      internal::locale_snapshot(internal::thread_locale).__data[LC_COLLATE];
  if (category->collation)
    collation = &*category->collation;
  auto converter = internal::RestartableConverter::current();
#else
  // Overlay builds retain the original C-locale behavior rather than
  // interpreting another libc's opaque locale records.
  internal::RestartableConverter converter(false);
#endif
  int saved_errno = libc_errno;
  auto result = internal::match_path_pattern(
      pattern, string, flags, converter, internal::current_ctype(), collation);
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
  libc_errno = saved_errno;
  return *result ? 0 : FNM_NOMATCH;
}
} // namespace LIBC_NAMESPACE_DECL
