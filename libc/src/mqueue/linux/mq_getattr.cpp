//===-- Linux implementation of mq_getattr --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/mqueue/mq_getattr.h"

#include "src/mqueue/linux/mqueue_utils.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, mq_getattr, (mqd_t mqdes, struct mq_attr *attr)) {
  return mqueue::getsetattr(mqdes, nullptr, attr);
}

} // namespace LIBC_NAMESPACE_DECL
