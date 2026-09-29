//===-- MMIX Linux terminal thread reclamation ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_RECLAIM_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_RECLAIM_H

#include "resources.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// Requires a pinned control, blocked signals and suppressed cancellation.
// Wakeups alone never authorize reclamation; the kernel must clear the word.
void await_thread_clear(ThreadControl &control);

// The exclusive owner consumes its creator pin (AbortOwner), API pin
// (JoinOwner), or lifecycle ownership (Detached). Copy any result first.
// No control access is allowed after return. A nonzero result retains failed
// mappings in owner storage and requires fatal handling, never join success.
long reclaim_thread(ThreadControl &control, ThreadOwner owner,
                    ThreadResources &retained);

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
