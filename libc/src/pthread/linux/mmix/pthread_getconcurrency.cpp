//===-- MMIX Linux pthread_getconcurrency ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_getconcurrency.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/concurrency_hint.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_getconcurrency, (void)) {
  return mmix::get_concurrency_hint();
}
} // namespace LIBC_NAMESPACE_DECL
