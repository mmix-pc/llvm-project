//===-- Linux implementation of mq_notify ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/mqueue/mq_notify.h"
#include "src/mqueue/linux/mq_notify_utils.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, mq_notify,
                   (mqd_t mqdes, const struct sigevent *notification)) {
  return mqueue::notify(mqdes, notification);
}

} // namespace LIBC_NAMESPACE_DECL
