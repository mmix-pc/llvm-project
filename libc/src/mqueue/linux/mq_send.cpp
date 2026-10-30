//===-- Linux implementation of mq_send -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/mqueue/mq_send.h"

#include "src/__support/common.h"
#include "src/mqueue/mq_timedsend.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, mq_send,
                   (mqd_t mqdes, const char *msg_ptr, size_t msg_len,
                    unsigned msg_prio)) {
  return LIBC_NAMESPACE::mq_timedsend(mqdes, msg_ptr, msg_len, msg_prio,
                                      nullptr);
}

} // namespace LIBC_NAMESPACE_DECL
