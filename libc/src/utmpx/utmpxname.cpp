//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/utmpx/utmpxname.h"
#include "src/__support/common.h"
#include "src/utmp/utmp_engine.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, utmpxname, (const char *path)) {
  return utmp_backend::name(path);
}

} // namespace LIBC_NAMESPACE_DECL
