//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// MMIX Linux implementation of sem_unlink.
///
//===----------------------------------------------------------------------===//

#include "src/semaphore/sem_unlink.h"

#include "hdr/errno_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "src/__support/macros/null_check.h"
#include "src/semaphore/linux/semaphore.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, sem_unlink, (const char *name)) {
  LIBC_CRASH_ON_NULLPTR(name);

  if (int err = Semaphore::unlink(name)) {
    // Linux sticky-directory rejection uses EPERM; POSIX sem_unlink
    // reports permission denial as EACCES. Preserve other errors.
    libc_errno = err == EPERM ? EACCES : err;
    return -1;
  }
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
