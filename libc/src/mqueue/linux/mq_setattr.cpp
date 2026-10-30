//===-- Linux implementation of mq_setattr --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/mqueue/mq_setattr.h"

#include "src/mqueue/linux/mqueue_utils.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, mq_setattr,
                   (mqd_t mqdes, const struct mq_attr *__restrict attr,
                    struct mq_attr *__restrict oldattr)) {
  return mqueue::getsetattr(mqdes, attr, oldattr);
}

} // namespace LIBC_NAMESPACE_DECL
