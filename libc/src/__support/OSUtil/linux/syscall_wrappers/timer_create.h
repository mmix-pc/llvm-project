//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Implementation header for timer_create syscall wrapper.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_SYSCALL_WRAPPERS_TIMER_CREATE_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_SYSCALL_WRAPPERS_TIMER_CREATE_H

#include "hdr/signal_macros.h"
#include "hdr/types/clockid_t.h"
#include "hdr/types/pid_t.h"
#include "hdr/types/struct_sigevent.h"
#include "hdr/types/timer_t.h"
#include "src/__support/OSUtil/linux/kernel_sigevent.h"
#include "src/__support/OSUtil/linux/syscall.h" // For syscall_checked
#include "src/__support/common.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"
#include <sys/syscall.h> // For syscall numbers

namespace LIBC_NAMESPACE_DECL {
namespace linux_syscalls {

LIBC_INLINE ErrorOr<int> timer_create(clockid_t clockid, const sigevent *sevp,
                                      timer_t *timerid) {
#ifdef SYS_timer_create
  // The kernel writes an int, not the pointer-sized public timer_t.
  int kernel_timerid;
  KernelSigevent ksev;
  if (sevp)
    ksev = KernelSigevent(*sevp);
  // Keep the kernel's default notification and null-output error behavior.
  auto result =
      syscall_checked<int>(SYS_timer_create, clockid, sevp ? &ksev : nullptr,
                           timerid ? &kernel_timerid : nullptr);
  if (result)
    *timerid = reinterpret_cast<timer_t>(static_cast<long>(kernel_timerid));
  return result;
#else
#error "SYS_timer_create syscall not available."
#endif
}

} // namespace linux_syscalls
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_OSUTIL_SYSCALL_WRAPPERS_TIMER_CREATE_H
