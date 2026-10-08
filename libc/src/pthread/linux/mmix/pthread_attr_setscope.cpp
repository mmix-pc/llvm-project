//===-- MMIX Linux pthread_attr_setscope ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/pthread/pthread_attr_setscope.h"
#include "hdr/errno_macros.h"
#include "src/__support/common.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_attr_setscope,
                   (pthread_attr_t * attr, int value)) {
  if (value == PTHREAD_SCOPE_PROCESS)
    return ENOTSUP;
  if (value != PTHREAD_SCOPE_SYSTEM)
    return EINVAL;
  attr->__scope = value;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
