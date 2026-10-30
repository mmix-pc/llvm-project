//===-- MMIX Linux timer deletion ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/time/timer_delete.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/notification_service.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, timer_delete, (timer_t timer)) {
  int error = mmix::delete_notification_timer(timer);
  if (error)
    libc_errno = error;
  return error ? -1 : 0;
}
} // namespace LIBC_NAMESPACE_DECL
