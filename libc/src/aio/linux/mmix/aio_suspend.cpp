//===-- MMIX implementation of aio_suspend --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/aio/aio_suspend.h"

#include "hdr/errno_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/aio_service.h"
#include "src/__support/threads/linux/mmix/cancellation.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, aio_suspend,
                   (const struct aiocb *const *list, int count,
                    const struct timespec *timeout)) {
  if (count < 0 || (count && !list)) {
    libc_errno = EINVAL;
    return -1;
  }
  bool any = false;
  for (int i = 0; i < count; ++i)
    any |= list[i] != nullptr;
  if (!any) {
    // The selected empty-list compatibility policy creates no service and
    // does not inspect timeout, but remains an ordinary cancellation point.
    mmix::test_cancel();
    return 0;
  }
  int error = mmix::suspend_aio_requests_relative(list, size_t(count), timeout);
  if (!error)
    return 0;
  libc_errno = error == ERANGE ? EOVERFLOW : error;
  return -1;
}

} // namespace LIBC_NAMESPACE_DECL
