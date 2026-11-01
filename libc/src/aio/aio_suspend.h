//===-- Implementation header for aio_suspend ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_AIO_AIO_SUSPEND_H
#define LLVM_LIBC_SRC_AIO_AIO_SUSPEND_H

#include "hdr/types/struct_timespec.h"
#include "src/__support/macros/config.h"
#include "src/aio/types.h"

namespace LIBC_NAMESPACE_DECL {
int aio_suspend(const struct aiocb *const *list, int count,
                const struct timespec *timeout);
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_AIO_AIO_SUSPEND_H
