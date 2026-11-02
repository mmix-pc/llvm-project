//===-- MMIX process notification service -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_NOTIFICATION_SERVICE_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_NOTIFICATION_SERVICE_H

#include "hdr/types/clockid_t.h"
#include "hdr/types/struct_itimerspec.h"
#include "hdr/types/struct_sigevent.h"
#include "hdr/types/timer_t.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
// Positive errno. THREAD creation only; ordinary timer creation stays raw.
int create_notification_timer(clockid_t, const sigevent &, timer_t &);
int delete_notification_timer(timer_t);
int set_notification_timer(timer_t, int, const itimerspec *, itimerspec *);
int register_mq_notification(int, const sigevent &);

// Optional process hooks, called with application signals/cancellation blocked.
// Credential notification must remain enabled during helper retirement.
// Successful prepare holds admission until finish, including clone failure.
// A checkpoint without retirement preserves helper and callback ownership.
int prepare_notification_fork(bool minimal, bool *drained,
                              bool retire_helpers = true);
void finish_notification_fork(bool child);
// Lock-free, including quick_exit from a handler. No waiting or callbacks.
void close_notification_admission();
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
