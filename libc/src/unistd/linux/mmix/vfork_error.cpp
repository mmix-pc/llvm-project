//===-- MMIX Linux vfork error return -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/types/pid_t.h"
#include "src/__support/libc_errno.h"

#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#endif
#include "src/__support/threads/linux/mmix/credentials.h"
#include "src/__support/threads/linux/mmix/lifecycle.h"
#include "src/__support/threads/linux/mmix/thread_reaper.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {
LIBC_CONSTINIT LIBC_THREAD_LOCAL uint64_t parent_mask;
LIBC_CONSTINIT LIBC_THREAD_LOCAL int parent_errno;

void restore_mask(uint64_t mask) {
  if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                   reinterpret_cast<long>(&mask), 0, sizeof(mask)))
    __builtin_trap();
}
} // namespace
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

extern "C" [[gnu::visibility("hidden")]] long __llvm_libc_mmix_vfork_begin() {
  using namespace LIBC_NAMESPACE;
  using namespace LIBC_NAMESPACE::mmix;
  int saved_errno = libc_errno;
  uint64_t full = UINT64_MAX, saved = 0;
  long result = syscall_impl(SYS_rt_sigprocmask, SIG_BLOCK,
                             reinterpret_cast<long>(&full),
                             reinterpret_cast<long>(&saved), sizeof(saved));
  if (result < 0)
    return result;
  if (result)
    __builtin_trap();
  auto *caller = current_control;
  int error = ENOTSUP;
  if (caller && caller->fork_recovery.allows_thread_creation() &&
      !caller->cancellation_suppression.load() &&
      !caller->cancellation_transfer.load() &&
      !caller->cancellation_point.load()) {
    caller->cancellation_suppression.fetch_add(1);
    if (retire_reaper_for_fork(*caller)) {
      ThreadRegistryLock lock(thread_registry);
      if (lock.begin_vfork(*caller))
        error = 0;
    }
    if (error)
      caller->cancellation_suppression.fetch_sub(1);
  }
  if (error) {
    restore_mask(saved);
    return -error;
  }
  parent_mask = saved;
  parent_errno = saved_errno;
  // The child borrows TP: private notifications must not operate on its
  // suspended parent's control. Application masks remain unchanged.
  uint64_t private_mask = CREDENTIAL_MASK;
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
  private_mask |= CANCEL_MASK;
#endif
  restore_mask(saved | private_mask);
  return 0;
}
extern "C" [[gnu::visibility("hidden")]] long
__llvm_libc_mmix_vfork_end(long result) {
  using namespace LIBC_NAMESPACE;
  using namespace LIBC_NAMESPACE::mmix;
  restore_mask(UINT64_MAX);
  {
    ThreadRegistryLock lock(thread_registry);
    if (!lock.end_process_operation())
      __builtin_trap();
  }
  if (current_control->cancellation_suppression.fetch_sub(1) != 1)
    __builtin_trap();
  libc_errno = parent_errno;
  restore_mask(parent_mask);
  return result;
}
#endif

extern "C" [[gnu::visibility("hidden")]] pid_t
__llvm_libc_mmix_vfork_error(long result) {
  libc_errno = static_cast<int>(-result);
  return -1;
}
