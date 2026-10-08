//===-- MMIX Linux pthread_attr_getscope ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/pthread/pthread_attr_getscope.h"
#include "hdr/errno_macros.h"
#include "src/__support/common.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_attr_getscope,
                   (const pthread_attr_t *__restrict attr,
                    int *__restrict value)) {
  *value = attr->__scope;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
