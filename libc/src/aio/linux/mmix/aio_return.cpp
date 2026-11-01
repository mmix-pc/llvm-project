//===-- MMIX implementation of aio_return -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/aio/aio_return.h"

#include "public_result.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/aio_service.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(ssize_t, aio_return, (struct aiocb *aiocbp)) {
  auto result = mmix::take_aio_result(aiocbp);
  if (!result) {
    libc_errno = result.error() == EINPROGRESS ? EINVAL : result.error();
    return -1;
  }
  // This is the operation's return status, not a failure of result retrieval.
  // The operation's error is obtained separately through aio_error().
  return result->value;
}

} // namespace LIBC_NAMESPACE_DECL
