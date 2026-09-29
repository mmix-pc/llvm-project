//===-- MMIX Linux internal thread signals ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_SIGNAL_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_SIGNAL_H

#include "lifecycle.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// Internal callers suppress cancellation. Returns a direct error number and
// preserves errno. Peer operations mask signals until pins/leases are released;
// self delivery holds neither, so a handler can leave the calling activation.
int signal_thread(ThreadAttributes *handle, int signal);

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
