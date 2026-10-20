//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/utmpx/pututxline.h"
#include "src/__support/common.h"
#include "src/utmp/utmp_engine.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(struct utmpx *, pututxline, (const struct utmpx *record)) {
  return static_cast<struct utmpx *>(
      utmp_backend::put(utmp_backend::Family::UTMPX, record));
}

} // namespace LIBC_NAMESPACE_DECL
