//===-- MMIX forced cleanup observations ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_FORCED_CLEANUP_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_FORCED_CLEANUP_H

#include "unwind_root.h"
#include "mmix/CursorLifetime.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

struct ForcedCleanupWitness {
  UnwindRoot root{};
  void *result = nullptr;
  MMIXCursorCheckpoint base{};
  MMIXForcedCheckpoint continuation{};
};

// Owner-thread borrow: exclude application signals throughout capture/validate
// and keep the same forced activation suspended. Discard when it advances or
// ends; equality is not an ABA/history guarantee. Neither call changes masks,
// looks up metadata, locks, retains/releases images or dereferences saved
// pointers. Capture failure leaves out unchanged; untracked builds return false.
//
// Requires a terminal cleanup claim; the cancellation type bit alone neither
// proves nor excludes synchronous forced exit. Binds the current Thread root,
// result, pre-forced base and libunwind's actual Stopped/Landing identities to
// libc's live exception with its fixed class,
// disposal function, stop and argument. A nonempty initial base, signal policy,
// handoff, unsafe metadata policy or different kernel chain is not observable.
// Landing borrows persistent held tokens, never a fabricated stack cursor.
//
// This is not callback certification, exit/cancel provenance, registry census,
// loader-accounting validation or fork/child continuation authority. It does not
// reset forced ownership or authorize restoring an ordinary fork context.

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
