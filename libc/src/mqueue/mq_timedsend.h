//===-- Implementation header for mq_timedsend --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_MQUEUE_MQ_TIMEDSEND_H
#define LLVM_LIBC_SRC_MQUEUE_MQ_TIMEDSEND_H

#include "hdr/types/size_t.h"
#include "hdr/types/struct_timespec.h"
#include "src/__support/macros/config.h"
#include "src/mqueue/types.h"

namespace LIBC_NAMESPACE_DECL {

int mq_timedsend(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
                 unsigned msg_prio, const struct timespec *abs_timeout);

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_MQUEUE_MQ_TIMEDSEND_H
