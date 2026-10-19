//===-- MMIX Linux same-stack process creation ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "fork_transport.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/exit.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/threads/thread.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
ForkResult fork_process() {
  auto *attributes = internal::self.attrib;
  if (!attributes)
    return {-EINVAL, 0};
  pid_t previous = attributes->tid;
  // Linux copies both stacks; the child resumes the ordinary syscall leaf.
  long result = syscall_impl<long>(SYS_clone, SIGCHLD, 0, 0, 0, 0);
  if (!result) {
    long tid = syscall_impl<long>(SYS_gettid);
    if (tid <= 0 || tid > cpp::numeric_limits<pid_t>::max())
      internal::exit(127);
    attributes->tid = pid_t(tid);
  }
  return {result, previous};
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
