//===-- MMIX Linux internal detach ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "thread_detach.h"
#include "hdr/errno_macros.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
int detach_thread(ThreadAttributes *handle) {
  ThreadRegistryLock lock(thread_registry);
  ThreadControl *control = lock.pin(handle);
  if (!control)
    return ESRCH;
  int error = control->retains_resources_until_process_exit() ? ENOTSUP
              : lock.detach(*control) ? 0 : EINVAL;
  if (!lock.unpin(*control))
    __builtin_trap();
  return error;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
