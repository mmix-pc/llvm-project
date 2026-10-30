//===-- Implementation header for mq_send -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_MQUEUE_MQ_SEND_H
#define LLVM_LIBC_SRC_MQUEUE_MQ_SEND_H

#include "hdr/types/size_t.h"
#include "src/__support/macros/config.h"
#include "src/mqueue/types.h"

namespace LIBC_NAMESPACE_DECL {

int mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
            unsigned msg_prio);

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_MQUEUE_MQ_SEND_H
