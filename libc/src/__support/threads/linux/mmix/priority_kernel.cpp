//===-- MMIX Linux scheduler transport ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "hdr/errno_macros.h"
#include "priority_protection_state.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include <sys/syscall.h>
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
int priority_range(int &minimum, int &maximum) {
  long low = syscall_impl<long>(SYS_sched_get_priority_min, 1);
  long high = syscall_impl<long>(SYS_sched_get_priority_max, 1);
  if (low < 0)
    return int(-low);
  if (high < 0)
    return int(-high);
  if (low < 1 || high < low || high > 127)
    return ENOTSUP;
  minimum = int(low);
  maximum = int(high);
  return 0;
}
int priority_kernel_read_for(int tid, int &policy, int &priority) {
  long result = syscall_impl<long>(SYS_sched_getscheduler, tid);
  if (result < 0)
    return int(-result);
  struct {
    int priority;
  } parameter{};
  long status = syscall_impl<long>(SYS_sched_getparam, tid, &parameter);
  if (status < 0)
    return int(-status);
  policy = int(result);
  priority = parameter.priority;
  return 0;
}
int priority_kernel_set_for(int tid, int policy, int priority) {
  struct {
    int priority;
  } parameter{priority};
  long result =
      syscall_impl<long>(SYS_sched_setscheduler, tid, policy, &parameter);
  return result < 0 ? int(-result) : 0;
}
int priority_kernel_read(int &policy, int &priority) {
  return priority_kernel_read_for(0, policy, priority);
}
int priority_kernel_set(int policy, int priority) {
  return priority_kernel_set_for(0, policy, priority);
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
