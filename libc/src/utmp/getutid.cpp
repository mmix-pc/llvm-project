//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/utmp/getutid.h"
#include "src/__support/common.h"
#include "src/utmp/utmp_engine.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(struct utmp *, getutid, (const struct utmp *key)) {
  return static_cast<struct utmp *>(utmp_backend::get(
      utmp_backend::Family::UTMP, utmp_backend::Search::ID, key));
}

} // namespace LIBC_NAMESPACE_DECL
