//===-- Implementation header for lio_listio -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_AIO_LIO_LISTIO_H
#define LLVM_LIBC_SRC_AIO_LIO_LISTIO_H

#include "src/__support/macros/config.h"
#include "src/aio/types.h"

namespace LIBC_NAMESPACE_DECL {
int lio_listio(int mode, struct aiocb *const *__restrict list,
               int count, struct sigevent *__restrict notification);
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_AIO_LIO_LISTIO_H
