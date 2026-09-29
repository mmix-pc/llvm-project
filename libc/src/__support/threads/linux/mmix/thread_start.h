//===-- MMIX Linux child startup -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_START_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_START_H

#include "thread_entry.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// Lifecycle continuations must not return. Keep ordinary call declarations so
// the startup frame can trap on an unexpected return instead of tail-calling.
// The run continuation restores the saved mask only after GO; abort must not
// run user callbacks. Both retain live mappings through terminal exit.
extern "C" __attribute__((visibility("hidden"))) void
__llvm_libc_mmix_thread_run(ThreadControl *control);
extern "C" __attribute__((visibility("hidden"))) void
__llvm_libc_mmix_thread_abort(ThreadControl *control);

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
