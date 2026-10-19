//===-- MMIX vfork admission and parent-only rollback tests ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Execute production admission with injected kernel masks and reaper status.
#include <cassert>
#include <csignal>
#include <initializer_list>
#include <linux/futex.h>
#include <sys/syscall.h>

#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_X86_64_SYSCALL_H
#define __mmix__ 1
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#undef __mmix__
#include "src/__support/threads/linux/mmix/lifecycle.cpp"
#include "src/unistd/linux/mmix/vfork_error.cpp"
#include "src/errno/libc_errno.cpp"

namespace M = LIBC_NAMESPACE::mmix;
static uint64_t mask = 0x4000;
static bool fail_mask, reaper_ready = true;
static unsigned retirements;
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
LIBC_THREAD_LOCAL ThreadControl *current_control;
bool retire_reaper_for_fork(ThreadControl &caller) {
  assert(mask == UINT64_MAX && caller.cancellation_suppression.load() == 1);
  ++retirements;
  libc_errno = ERANGE;
  ThreadRegistryLock lock(thread_registry);
  return reaper_ready && lock.sole_fork_caller(caller);
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

extern "C" long __llvm_libc_mmix_syscall(unsigned long number, long how,
                                       long set, long old, long size, long,
                                       long) {
  if (number == SYS_futex) {
    assert(set == FUTEX_WAKE_PRIVATE);
    return 0;
  }
  assert(number == SYS_rt_sigprocmask && size == 8);
  if (fail_mask) {
    fail_mask = false;
    return -EFAULT;
  }
  if (old)
    *reinterpret_cast<uint64_t *>(old) = mask;
  if (how == SIG_BLOCK)
    mask |= *reinterpret_cast<uint64_t *>(set);
  else {
    assert(how == SIG_SETMASK);
    mask = *reinterpret_cast<uint64_t *>(set);
  }
  return 0;
}

int main() {
  M::ThreadControl caller, worker;
  M::current_control = &caller;
  caller.attributes.tid = 17;
  {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.register_main(caller));
  }
  fail_mask = true;
  assert(__llvm_libc_mmix_vfork_begin() == -EFAULT && !retirements);
  reaper_ready = false;
  assert(__llvm_libc_mmix_vfork_begin() == -ENOTSUP);
  assert(mask == 0x4000 && !caller.cancellation_suppression.load());
  reaper_ready = true;
  for (long result : {-EAGAIN, -ENOMEM, 43}) {
    libc_errno = EDOM;
    assert(!__llvm_libc_mmix_vfork_begin());
    uint64_t expected_mask = 0x4000 | M::CREDENTIAL_MASK;
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
    expected_mask |= M::CANCEL_MASK;
#endif
    assert(mask == expected_mask);
    assert(caller.cancellation_suppression.load() == 1);
    assert(__llvm_libc_mmix_vfork_begin() == -ENOTSUP);
    assert(caller.cancellation_suppression.load() == 1);
    {
      M::ThreadRegistryLock lock(M::thread_registry);
      assert(!lock.reserve() && !lock.begin_helper_creation());
    }
    // Shared child errno must not corrupt the successful parent's value.
    libc_errno = ENOENT;
    assert(__llvm_libc_mmix_vfork_end(result) == result);
    assert(libc_errno == EDOM && mask == 0x4000);
    assert(!caller.cancellation_suppression.load());
    assert(caller.attributes.tid == 17);
    if (result < 0) {
      assert(__llvm_libc_mmix_vfork_error(result) == -1);
      assert(libc_errno == -result);
    }
  }
  caller.cancellation_transfer.store(1);
  assert(__llvm_libc_mmix_vfork_begin() == -ENOTSUP && mask == 0x4000);
  caller.cancellation_transfer.store(0);
  caller.cancellation.request();
  assert(caller.cancellation.ready());
  assert(!__llvm_libc_mmix_vfork_begin());
  assert(__llvm_libc_mmix_vfork_end(45) == 45);
  assert(caller.cancellation.ready());
  {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.reserve() && lock.insert(worker, false));
    assert(lock.start(worker) && lock.drop_creator(worker));
  }
  assert(__llvm_libc_mmix_vfork_begin() == -ENOTSUP);
  // Recreate a survivor with history but no live sibling or pending creator.
  M::thread_registry.restore_fork_child(
      caller, LIBC_NAMESPACE::internal::ForkRecovery::child(true, true));
  {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.reserve());
  }
  assert(__llvm_libc_mmix_vfork_begin() == -ENOTSUP);
  {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.cancel_reservation());
  }
  assert(!__llvm_libc_mmix_vfork_begin());
  assert(__llvm_libc_mmix_vfork_end(44) == 44);
  assert(mask == 0x4000 && !caller.cancellation_suppression.load());
}
