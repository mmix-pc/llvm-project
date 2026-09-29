//===-- MMIX Linux internal thread join -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "thread_join.h"
#include "hdr/errno_macros.h"
#include "src/__support/threads/thread.h"
#include "thread_reclaim.h"

#if LIBC_THREAD_MODE != LIBC_THREAD_MODE_PLATFORM || \
    LIBC_ERRNO_MODE != LIBC_ERRNO_MODE_THREAD_LOCAL
#error "MMIX Linux thread join requires platform TLS and TLS errno"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

int claim_thread_join(ThreadAttributes *handle, ThreadJoinClaim &claim) {
  if (claim.control)
    __builtin_trap();
  ThreadAttributes *self = internal::self.attrib;
  if (!self)
    return EINVAL;
  if (self == handle)
    return EDEADLK;
  ThreadRegistryLock lock(thread_registry);
  ThreadControl *control = lock.pin(handle);
  if (!control)
    return ESRCH;
  int error = 0;
  // Main's static storage needs a distinct join policy; worker reclamation
  // must not acquire it merely because main has a registry entry.
  if (control->retains_resources_until_process_exit())
    error = ENOTSUP;
  else if (lock.owner(*control) != ThreadOwner::Joinable)
    error = EINVAL;
  else if (self->joiner.load(cpp::MemoryOrder::ACQUIRE) == handle)
    error = EDEADLK;
  if (error) {
    if (!lock.unpin(*control))
      __builtin_trap();
    return error;
  }
  if (!lock.claim_join(*control))
    __builtin_trap();
  control->attributes.joiner.store(self, cpp::MemoryOrder::RELEASE);
  claim.control = control;
  claim.caller = self;
  return 0;
}

void abandon_thread_join(ThreadJoinClaim &claim) {
  if (!claim.control || claim.caller != internal::self.attrib)
    __builtin_trap();
  ThreadRegistryLock lock(thread_registry);
  if (!lock.release_join(*claim.control))
    __builtin_trap();
  claim.control->attributes.joiner.store(nullptr, cpp::MemoryOrder::RELEASE);
  if (!lock.unpin(*claim.control))
    __builtin_trap();
  claim.control = nullptr;
  claim.caller = nullptr;
}

void commit_thread_join(ThreadJoinClaim &claim, ThreadReturnValue &result) {
  if (!claim.control || claim.caller != internal::self.attrib)
    __builtin_trap();
  ThreadControl *control = claim.control;
  await_thread_clear(*control);
  ThreadReturnValue saved;
  {
    ThreadRegistryLock lock(thread_registry);
    // Kernel death without runtime cleanup is not a successful normal join.
    if (!lock.join_result(*control, saved))
      __builtin_trap();
  }
  // Noncancelable commit: the reclaimer consumes this claim's API pin.
  // Copy the result first; neither target nor claim may be reused afterward.
  claim.control = nullptr;
  claim.caller = nullptr;
  ThreadResources retained;
  if (reclaim_thread(*control, ThreadOwner::JoinOwner, retained))
    __builtin_trap();
  result = saved;
}

int join_thread(ThreadAttributes *handle, ThreadReturnValue &result) {
  ThreadJoinClaim claim;
  int error = claim_thread_join(handle, claim);
  if (error)
    return error;
  commit_thread_join(claim, result);
  return 0;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
