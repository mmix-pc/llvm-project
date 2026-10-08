//===-- MMIX Linux scheduled pthread creation -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/pthread/pthread_create.h"
#include "config/app.h"
#include "hdr/errno_macros.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/thread_create.h"
#include "src/__support/threads/thread.h"
#include "src/pthread/pthread_attr_init.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_create,
                   (pthread_t *__restrict output,
                    const pthread_attr_t *__restrict attr,
                    __pthread_start_t runner, void *argument)) {
  pthread_attr_t defaults;
  if (!attr) {
    LIBC_NAMESPACE::pthread_attr_init(&defaults);
    attr = &defaults;
  }
  if ((attr->__detachstate != PTHREAD_CREATE_JOINABLE &&
       attr->__detachstate != PTHREAD_CREATE_DETACHED) ||
      (attr->__stacksize && attr->__stacksize < PTHREAD_STACK_MIN) ||
      attr->__guardsize % EXEC_PAGESIZE ||
      (attr->__inheritsched != PTHREAD_INHERIT_SCHED &&
       attr->__inheritsched != PTHREAD_EXPLICIT_SCHED))
    return EINVAL;
  if (attr->__scope != PTHREAD_SCOPE_SYSTEM)
    return attr->__scope == PTHREAD_SCOPE_PROCESS ? ENOTSUP : EINVAL;
  mmix::ThreadPreparation request;
  request.runner.posix_runner = runner;
  request.argument = argument;
  request.stack = attr->__stack;
  request.stack_size = attr->__stacksize;
  request.guard_size = attr->__guardsize;
  request.detached = attr->__detachstate == PTHREAD_CREATE_DETACHED;
  request.image = app.tls;
  request.page_size = app.page_size;
  if (attr->__inheritsched == PTHREAD_EXPLICIT_SCHED) {
    if (int error = mmix::scheduling_validate(attr->__schedpolicy,
                                              attr->__schedpriority))
      return error;
    request.apply_scheduling = true;
    request.scheduling_policy = attr->__schedpolicy;
    request.scheduling_priority = attr->__schedpriority;
  }
  ThreadAttributes *created = nullptr;
  auto result = mmix::create_thread(request, created);
  if (result.preparation.retained.control.size ||
      result.preparation.retained.stack.size ||
      result.preparation.retained.tls.size)
    __builtin_trap();
  if (result.error) {
    int error = int(-result.error);
    return error == EPERM || error == EINVAL ? error : EAGAIN;
  }
  static_assert(sizeof(pthread_t) == sizeof(Thread));
  reinterpret_cast<Thread *>(output)->attrib = created;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
