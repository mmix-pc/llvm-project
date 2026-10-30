//===-- MMIX managed timer registrations ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_NOTIFICATION_TIMERS_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_NOTIFICATION_TIMERS_H

#include "notification_callback.h"
#include "hdr/types/clockid_t.h"
#include "hdr/types/struct_itimerspec.h"
#include "hdr/types/timer_t.h"
#include "src/__support/OSUtil/linux/notification_transport.h"
#include "src/__support/threads/notification_registry.h"
#include "src/__support/threads/raw_mutex.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// The enclosing service holds a dispatcher Operation, suppresses cancellation
// and blocks application signals through each call. The helper target must
// outlive all registrations. This table never invokes application callbacks.
// Fork must park the enclosing service; copying this table alone is not a
// child recovery protocol. Cookies survive helper retirement/restart.
class NotificationTimers {
  struct Record : NotificationRegistry::Node {
    Record *next = nullptr;
    cpp::Atomic<Record *> signal_next{nullptr};
    Record *retired_next = nullptr;
    NotificationCallback *callback;
    int id = -1;
    explicit Record(NotificationCallback *callback) : callback(callback) {}
  };
  struct alignas(8) TableMutex { RawMutex value; } mutex;
  NotificationRegistry registry;
  Record *head = nullptr;
  Record *retired = nullptr;
  cpp::Atomic<Record *> signal_head{nullptr};
  cpp::Atomic<uint64_t> signal_readers{0};

  static void destroy(Record *);

public:
  NotificationTimers() = default;
  NotificationTimers(const NotificationTimers &) = delete;
  NotificationTimers &operator=(const NotificationTimers &) = delete;

  // Only THREAD admission; returns positive errno, leaving output unchanged
  // on failure. Allocation and callback-image retention occur outside the lock.
  int create(clockid_t, const sigevent &, const TLSImage &, uintptr_t page_size,
             int helper_tid, timer_t &output);
  // Also handles timers without a THREAD side-table record. Kernel-ID reuse,
  // event acceptance and association removal share the same short gate.
  int remove(timer_t);
  // Signal-safe: no table lock, allocation, cancellation scope or reclamation.
  int settime(timer_t, int flags, const itimerspec *, itimerspec *old);
  void dispatch(const signalfd_siginfo &);
  // Call only after the outer service has excluded all operations/intake.
  bool empty() const { return head == nullptr; }
  // Reclaims retired records only after their signal-safe readers have drained.
  // This is ordinary service work, never called from settime or a handler.
  bool collect_retired();
};

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
