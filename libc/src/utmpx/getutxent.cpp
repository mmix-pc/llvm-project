//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/utmpx/getutxent.h"
#include "src/__support/common.h"
#include "src/utmp/utmp_engine.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(struct utmpx *, getutxent, ()) {
  return static_cast<struct utmpx *>(utmp_backend::get(
      utmp_backend::Family::UTMPX, utmp_backend::Search::NEXT));
}

} // namespace LIBC_NAMESPACE_DECL
