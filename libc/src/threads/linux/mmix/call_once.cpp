//===-- MMIX Linux C once initialization ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/threads/call_once.h"
#include "src/__support/common.h"
#include "src/pthread/pthread_once.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(void, call_once,
                   (once_flag *flag, __call_once_func_t function)) {
  LIBC_NAMESPACE::pthread_once(flag, function);
}

} // namespace LIBC_NAMESPACE_DECL
