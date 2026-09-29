//===-- MMIX Linux internal thread join -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_JOIN_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_JOIN_H

#include "lifecycle.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// Same-thread ownership token, not a public handle. An active claim must be
// explicitly abandoned or committed; copying would duplicate its API pin.
class ThreadJoinClaim {
  ThreadControl *control = nullptr;
  ThreadAttributes *caller = nullptr;
  friend int claim_thread_join(ThreadAttributes *, ThreadJoinClaim &);
  friend void abandon_thread_join(ThreadJoinClaim &);
  friend void commit_thread_join(ThreadJoinClaim &, ThreadReturnValue &);

public:
  ThreadJoinClaim() = default;
  ThreadJoinClaim(const ThreadJoinClaim &) = delete;
  ThreadJoinClaim &operator=(const ThreadJoinClaim &) = delete;
};

// Internal callers block signals and suppress cancellation through these
// operations. Later cancellable waits need a cleanup that abandons the claim
// before unwinding, then a noncancelable commit. No cancellation API is admitted.
// Lookup compares handle addresses under the registry lock, never dereferencing
// an unpinned handle. Errors are positive errno values and leave output intact.
int claim_thread_join(ThreadAttributes *handle, ThreadJoinClaim &claim);
void abandon_thread_join(ThreadJoinClaim &claim);
void commit_thread_join(ThreadJoinClaim &claim, ThreadReturnValue &result);
int join_thread(ThreadAttributes *handle, ThreadReturnValue &result);

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
