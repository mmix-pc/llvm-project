//===-- Implementation of duplocale ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/duplocale.h"
#include "hdr/errno_macros.h"
#include "hdr/func/malloc.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "src/locale/locale.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(locale_t, duplocale, (locale_t loc)) {
  if (!loc) {
    libc_errno = EINVAL;
    return nullptr;
  }
  auto snapshot = internal::locale_snapshot(loc);
  auto copy = static_cast<locale_t>(::malloc(sizeof(__locale_t)));
  if (!copy) {
    libc_errno = ENOMEM;
    return nullptr;
  }
  *copy = snapshot;
  return copy;
}

} // namespace LIBC_NAMESPACE_DECL
