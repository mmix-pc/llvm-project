//===-- MMIX AIO public error translation -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_AIO_LINUX_MMIX_PUBLIC_RESULT_H
#define LLVM_LIBC_SRC_AIO_LINUX_MMIX_PUBLIC_RESULT_H

#include "hdr/errno_macros.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

LIBC_INLINE int aio_public_submission_result(int error) {
  if (!error)
    return 0;
  // Resource admission failure is EAGAIN, regardless of which private resource
  // (request storage, a retained fd or the ring) could not be created.
  libc_errno = (error == ENOMEM || error == EMFILE || error == ENFILE)
                   ? EAGAIN
                   : error;
  return -1;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_AIO_LINUX_MMIX_PUBLIC_RESULT_H
