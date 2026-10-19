//===-- MMIX Linux public thread signal mask ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/signal/pthread_sigmask.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/signal/sigprocmask.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_sigmask,
                   (int how, const sigset_t *__restrict set,
                    sigset_t *__restrict old)) {
  int saved = libc_errno;
  int result = LIBC_NAMESPACE::sigprocmask(how, set, old);
  int error = result ? libc_errno : 0;
  libc_errno = saved;
  return error;
}
} // namespace LIBC_NAMESPACE_DECL
