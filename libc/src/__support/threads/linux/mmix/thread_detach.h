//===-- MMIX Linux internal detach ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_DETACH_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_DETACH_H

#include "lifecycle.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
// Caller blocks signals and suppresses cancellation. Detach never waits for
// the target or releases its mappings; the independent reaper owns that work.
int detach_thread(ThreadAttributes *handle);
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
