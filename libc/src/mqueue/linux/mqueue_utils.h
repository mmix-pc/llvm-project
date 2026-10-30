//===-- Linux message queue record conversion ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_MQUEUE_LINUX_MQUEUE_UTILS_H
#define LLVM_LIBC_SRC_MQUEUE_LINUX_MQUEUE_UTILS_H

#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/sanitizer.h"
#include "src/mqueue/types.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mqueue {

// The native Linux record uses signed kernel longs, including four reserved
// words. Compat and x32 layouts need a separate ABI review before admission.
static_assert(sizeof(long) == 8 && sizeof(void *) == 8,
              "message queues require the native LP64 Linux ABI");
static_assert(sizeof(mqd_t) == sizeof(int));

struct KernelMqAttr {
  long flags;
  long maxmsg;
  long msgsize;
  long curmsgs;
  long reserved[4];
};
static_assert(sizeof(KernelMqAttr) == 8 * sizeof(long));

LIBC_INLINE int getsetattr(mqd_t mqdes, const struct mq_attr *attr,
                           struct mq_attr *oldattr) {
  KernelMqAttr input = {};
  KernelMqAttr output = {};
  // Only mq_flags is an input to mq_setattr; the other members may be unset.
  if (attr)
    input.flags = attr->mq_flags;
  int ret = LIBC_NAMESPACE::syscall_impl<int>(SYS_mq_getsetattr, mqdes,
                                              attr ? &input : nullptr,
                                              oldattr ? &output : nullptr);
  if (ret < 0) {
    libc_errno = -ret;
    return -1;
  }
  if (oldattr) {
    LIBC_MSAN_UNPOISON(&output, sizeof(output));
    *oldattr = {};
    oldattr->mq_flags = output.flags;
    oldattr->mq_maxmsg = output.maxmsg;
    oldattr->mq_msgsize = output.msgsize;
    oldattr->mq_curmsgs = output.curmsgs;
  }
  return ret;
}

} // namespace mqueue
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_MQUEUE_LINUX_MQUEUE_UTILS_H
