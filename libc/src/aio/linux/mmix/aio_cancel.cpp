//===-- MMIX implementation of aio_cancel -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/aio/aio_cancel.h"

#include "public_result.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/aio_service.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, aio_cancel, (int descriptor, struct aiocb *aiocbp)) {
  auto result = mmix::cancel_aio_requests(descriptor, aiocbp);
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
  return *result;
}

} // namespace LIBC_NAMESPACE_DECL
