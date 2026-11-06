//===-- MMIX Linux readdir without a deferred cancellation point -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/dirent/readdir.h"
#include "hdr/errno_macros.h"
#include "src/__support/File/dir.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation.h"
#include <pthread.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(struct ::dirent *, readdir, (::DIR *directory)) {
  // Issue 8 requires thread safety for distinct directory streams, but does
  // not list readdir as a cancellation point. Its shared refill helper also
  // serves the deliberately cancelable readdir_r/posix_getdents operations.
  int previous = PTHREAD_CANCEL_DISABLE;
  int error = mmix::set_cancel_state(PTHREAD_CANCEL_DISABLE, &previous);
  if (error && error != ENOTSUP)
    __builtin_trap();

  auto result = reinterpret_cast<Dir *>(directory)->read();
  // Dir::read has released its mutex. A pending deferred request stays pending
  // when ENABLE is restored; asynchronous delivery can no longer strand it.
  if (!error && mmix::set_cancel_state(previous, nullptr))
    __builtin_trap();
  if (!result) {
    libc_errno = result.error();
    return nullptr;
  }
  return result.value();
}

} // namespace LIBC_NAMESPACE_DECL
