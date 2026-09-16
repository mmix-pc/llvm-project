//===-- Linux implementation of sigaltstack -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/signal/sigaltstack.h"
#include "hdr/types/stack_t.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"

#include "src/__support/common.h"

#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, sigaltstack,
                   (const stack_t *__restrict ss, stack_t *__restrict oss)) {
  // Leave flag, size and current-stack validation to the kernel, including
  // SS_AUTODISARM and disabling a stack with a zero-sized descriptor.
  int ret = LIBC_NAMESPACE::syscall_impl<int>(SYS_sigaltstack, ss, oss);
  if (ret < 0) {
    libc_errno = -ret;
    return -1;
  }
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
