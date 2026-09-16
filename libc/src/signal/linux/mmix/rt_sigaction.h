//===-- MMIX Linux sigaction syscall adapter ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SIGNAL_LINUX_MMIX_RT_SIGACTION_H
#define LLVM_LIBC_SRC_SIGNAL_LINUX_MMIX_RT_SIGACTION_H

#include "kernel_sigaction.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/error_or.h"

#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// The caller supplies reviewed flags/restorer policy. This is only transport;
// it neither selects a vDSO entry nor makes a handler safe to register.
LIBC_INLINE ErrorOr<int> rt_sigaction(int signal, const struct sigaction *action,
                                    struct sigaction *old_action) {
  KernelSigaction kernel_new{}, kernel_old{};
  if (action)
    kernel_new = to_kernel_sigaction(*action);
  auto result = linux_syscalls::syscall_checked<int>(
      SYS_rt_sigaction, signal, action ? &kernel_new : nullptr,
      old_action ? &kernel_old : nullptr, sizeof(kernel_new.sa_mask));
  if (!result)
    return Error(result.error());
  if (old_action)
    *old_action = from_kernel_sigaction(kernel_old);
  return result;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_SIGNAL_LINUX_MMIX_RT_SIGACTION_H
