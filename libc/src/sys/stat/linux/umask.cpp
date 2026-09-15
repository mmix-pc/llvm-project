//===-- Linux implementation of umask -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/stat/umask.h"

#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(mode_t, umask, (mode_t mask)) {
  // Linux umask returns the previous mask and has no error result.
  return syscall_impl<mode_t>(SYS_umask, mask);
}

} // namespace LIBC_NAMESPACE_DECL
