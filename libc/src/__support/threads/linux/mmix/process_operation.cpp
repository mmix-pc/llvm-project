//===-- MMIX Linux process-operation admission ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "process_operation.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "lifecycle.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
int begin_process_operation() {
  uint64_t full = UINT64_MAX, saved = 0;
  long result =
      syscall_impl(SYS_rt_sigprocmask, SIG_BLOCK, reinterpret_cast<long>(&full),
                   reinterpret_cast<long>(&saved), sizeof(saved));
  if (result < 0)
    return static_cast<int>(-result);
  if (result != 0)
    __builtin_trap();
  bool admitted;
  {
    ThreadRegistryLock lock(thread_registry);
    admitted =
        current_control && lock.begin_process_operation(*current_control);
  }
  if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                   reinterpret_cast<long>(&saved), 0, sizeof(saved)))
    __builtin_trap();
  return admitted ? 0 : ENOTSUP;
}

void end_process_operation() {
  uint64_t full = UINT64_MAX, saved = 0;
  if (syscall_impl(SYS_rt_sigprocmask, SIG_BLOCK, reinterpret_cast<long>(&full),
                   reinterpret_cast<long>(&saved), sizeof(saved)))
    __builtin_trap();
  {
    ThreadRegistryLock lock(thread_registry);
    if (!lock.end_process_operation())
      __builtin_trap();
  }
  if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                   reinterpret_cast<long>(&saved), 0, sizeof(saved)))
    __builtin_trap();
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
