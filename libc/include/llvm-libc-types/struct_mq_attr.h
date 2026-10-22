//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_STRUCT_MQ_ATTR_H
#define LLVM_LIBC_TYPES_STRUCT_MQ_ATTR_H

struct mq_attr {
  long mq_flags;
  long mq_maxmsg;
  long mq_msgsize;
  long mq_curmsgs;
  long __reserved[4];
};

#endif // LLVM_LIBC_TYPES_STRUCT_MQ_ATTR_H
