//===-- Implementation header for mq_timedreceive -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_MQUEUE_MQ_TIMEDRECEIVE_H
#define LLVM_LIBC_SRC_MQUEUE_MQ_TIMEDRECEIVE_H

#include "hdr/types/size_t.h"
#include "hdr/types/ssize_t.h"
#include "hdr/types/struct_timespec.h"
#include "src/__support/macros/config.h"
#include "src/mqueue/types.h"

namespace LIBC_NAMESPACE_DECL {

ssize_t mq_timedreceive(mqd_t mqdes, char *__restrict msg_ptr, size_t msg_len,
                        unsigned *__restrict msg_prio,
                        const struct timespec *__restrict abs_timeout);

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_MQUEUE_MQ_TIMEDRECEIVE_H
