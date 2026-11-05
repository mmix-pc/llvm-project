//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CANCELLABLE_JOIN_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CANCELLABLE_JOIN_H

#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "src/__support/threads/linux/mmix/thread_join.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

class JoinSignalMask {
  uint64_t saved;

public:
  JoinSignalMask() {
    uint64_t full = UINT64_MAX;
    if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                     reinterpret_cast<long>(&full),
                     reinterpret_cast<long>(&saved), 8))
      __builtin_trap();
  }
  ~JoinSignalMask() {
    if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                     reinterpret_cast<long>(&saved), 0, 8))
      __builtin_trap();
  }
};

inline int cancellable_join(ThreadAttributes *handle,
                            ThreadReturnValue &result) {
  ThreadJoinClaim claim;
  int error;
  {
    JoinSignalMask mask;
    error = claim_thread_join(handle, claim);
  }
  if (error)
    return error;
  struct Cleanup {
    ThreadJoinClaim &claim;
    bool active = true;
    ~Cleanup() {
      if (active) {
        JoinSignalMask mask;
        abandon_thread_join(claim);
      }
    }
  } cleanup{claim};
  await_cancellable_join(claim);
  // No recovery descriptor is live during commit. A concurrent request stays
  // pending for the caller's next cancellation point, not a freed target.
  {
    JoinSignalMask mask;
    commit_thread_join(claim, result);
    cleanup.active = false;
  }
  return 0;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CANCELLABLE_JOIN_H
