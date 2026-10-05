//===-- MMIX Linux cancelable pthread join --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/thread_join.h"
#include "src/__support/threads/thread.h"
#include "src/pthread/pthread_join.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace {
class SignalMask {
  uint64_t saved;

public:
  SignalMask() {
    uint64_t full = UINT64_MAX;
    if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                     reinterpret_cast<long>(&full),
                     reinterpret_cast<long>(&saved), 8))
      __builtin_trap();
  }
  ~SignalMask() {
    if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                     reinterpret_cast<long>(&saved), 0, 8))
      __builtin_trap();
  }
};
} // namespace
LLVM_LIBC_FUNCTION(int, pthread_join, (pthread_t th, void **retval)) {
  static_assert(sizeof(pthread_t) == sizeof(Thread));
  auto *thread = reinterpret_cast<Thread *>(&th);
  mmix::ThreadJoinClaim claim;
  int error;
  {
    SignalMask mask;
    error = mmix::claim_thread_join(thread->attrib, claim);
  }
  if (error)
    return error;
  struct Cleanup {
    mmix::ThreadJoinClaim &claim;
    bool active = true;
    ~Cleanup() {
      if (active) {
        SignalMask mask;
        mmix::abandon_thread_join(claim);
      }
    }
  } cleanup{claim};
  mmix::await_cancellable_join(claim);
  ThreadReturnValue result;
  // No recovery descriptor is live during commit. A concurrent request stays
  // pending for the caller's next cancellation point, not a freed target.
  {
    SignalMask mask;
    mmix::commit_thread_join(claim, result);
    cleanup.active = false;
  }
  if (retval)
    *retval = result.posix_retval;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
