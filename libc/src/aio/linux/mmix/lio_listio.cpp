//===-- MMIX implementation of lio_listio ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/aio/lio_listio.h"

#include "public_result.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/aio_service.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, lio_listio,
                   (int mode, struct aiocb *const *__restrict list,
                    int count, struct sigevent *__restrict notification)) {
  int error;
  if (mode == LIO_NOWAIT)
    error = mmix::submit_aio_list_nowait(list, count, notification);
  else if (mode == LIO_WAIT)
    error = mmix::submit_aio_list_wait(list, count, notification);
  else
    error = EINVAL;
  return mmix::aio_public_submission_result(error);
}

} // namespace LIBC_NAMESPACE_DECL
