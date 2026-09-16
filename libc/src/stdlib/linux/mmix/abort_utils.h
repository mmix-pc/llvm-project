//===-- MMIX Linux abnormal termination --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_STDLIB_LINUX_MMIX_ABORT_UTILS_H
#define LLVM_LIBC_SRC_STDLIB_LINUX_MMIX_ABORT_UTILS_H

#include "src/__support/OSUtil/exit.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/raise.h"
#include "src/signal/linux/mmix/rt_sigaction.h"

namespace LIBC_NAMESPACE_DECL {
namespace abort_utils {

// FIXME: Reuse the common Linux algorithm once its restorer/vDSO selection is
// separable. Signal masking suffices for this single-thread profile; pthread
// support must coordinate SIGABRT disposition changes with sigaction/fork/spawn.
[[noreturn]] LIBC_INLINE void abort() {
  (void)linux_syscalls::raise(SIGABRT);

  // A returning handler or failed first raise must not allow abort to return.
  sigset_t full{{-1UL}};
  (void)linux_syscalls::rt_sigprocmask(SIG_BLOCK, &full, nullptr);
  struct sigaction action{};
  action.sa_handler = SIG_DFL;
  // The default disposition has no user handler and needs no restorer.
  (void)mmix::rt_sigaction(SIGABRT, &action, nullptr);
  (void)linux_syscalls::raise(SIGABRT);

  sigset_t pending{{1UL << (SIGABRT - 1)}};
  (void)linux_syscalls::rt_sigprocmask(SIG_UNBLOCK, &pending, nullptr);
  internal::exit(127);
}

} // namespace abort_utils
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_STDLIB_LINUX_MMIX_ABORT_UTILS_H
