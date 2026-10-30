//===-- Linux implementation of mq_open -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/mqueue/mq_open.h"

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/types/mode_t.h"
#include "src/mqueue/linux/mqueue_utils.h"
#include <stdarg.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(mqd_t, mq_open, (const char *name, int oflag, ...)) {
  if (name[0] != '/') {
    libc_errno = EINVAL;
    return -1;
  }

  mode_t mode = 0;
  struct mq_attr *attr = nullptr;
  if (oflag & O_CREAT) {
    va_list args;
    va_start(args, oflag);
    mode = va_arg(args, mode_t);
    attr = va_arg(args, struct mq_attr *);
    va_end(args);
  }

  mqueue::KernelMqAttr input = {};
  if (attr) {
    input.maxmsg = attr->mq_maxmsg;
    input.msgsize = attr->mq_msgsize;
  }
  // Linux expects one component, not a /dev/mqueue path. Strip exactly the
  // required leading slash and let the kernel reject empty or nested names.
  int ret = LIBC_NAMESPACE::syscall_impl<int>(SYS_mq_open, name + 1, oflag,
                                              mode, attr ? &input : nullptr);
  if (ret < 0) {
    libc_errno = -ret;
    return -1;
  }
  return ret;
}

} // namespace LIBC_NAMESPACE_DECL
