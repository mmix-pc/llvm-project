//===-- MMIX implementation of aio_fsync --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/aio/aio_fsync.h"

#include "public_result.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/aio_service.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, aio_fsync, (int operation, struct aiocb *aiocbp)) {
  mmix::AioSyncInput input;
  int error = mmix::snapshot_aio_sync_input(operation, aiocbp, input);
  if (!error)
    error = mmix::submit_aio_sync(input);
  return mmix::aio_public_submission_result(error);
}

} // namespace LIBC_NAMESPACE_DECL
