//===-- Implementation of wordexp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wordexp/wordexp.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/locale/locale.h"
#include "src/wordexp/runtime.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, wordexp,
                   (const char *__restrict words, wordexp_t *__restrict result,
                    int flags)) {
  // The caller keeps thread locale objects alive and synchronizes setlocale
  // and environment mutation, as for the other locale-sensitive interfaces.
  auto locale = internal::locale_snapshot(internal::thread_locale);
  const auto *ctype = locale.__data[LC_CTYPE];
  const auto *collate = locale.__data[LC_COLLATE];
  auto converter =
      ctype->chinese_encoding
          ? internal::RestartableConverter(*ctype->chinese_encoding)
          : internal::RestartableConverter(ctype->utf8);
  int saved_errno = libc_errno;
  auto outcome = internal::evaluate_wordexp_runtime(
      words, *result, flags, converter, ctype,
      collate->collation ? &*collate->collation : nullptr);
  libc_errno = outcome.error ? outcome.error : saved_errno;
  return outcome.status;
}
} // namespace LIBC_NAMESPACE_DECL
