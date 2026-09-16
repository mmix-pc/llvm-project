//===-- MMIX Linux sigprocmask implementation ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/signal/sigprocmask.h"

#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/rt_sigprocmask.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"

// FIXME: Reuse the common Linux provider once signal-set/mask operations are
// decoupled from signal_utils.h's handler, vDSO and locking dependencies.
namespace LIBC_NAMESPACE_DECL {

// Public capacity and the kernel transfer size are both one octa on MMIX.
static_assert(sizeof(unsigned long) == 8 && sizeof(sigset_t) == 8);
static_assert(NSIG == 65);

LLVM_LIBC_FUNCTION(int, sigprocmask,
                   (int how, const sigset_t *__restrict set,
                    sigset_t *__restrict oldset)) {
  auto result = linux_syscalls::rt_sigprocmask(how, set, oldset);
  if (result)
    return 0;
  libc_errno = result.error();
  return -1;
}

} // namespace LIBC_NAMESPACE_DECL
