//===-- MMIX private MQ notification transactions ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_NOTIFICATION_MQ_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_NOTIFICATION_MQ_H

#include "notification_callback.h"
#include "src/__support/OSUtil/linux/notification_transport.h"
#include "src/__support/threads/notification_registry.h"
#include "src/__support/threads/raw_mutex.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// Service-owned, THREAD-only transactions, not a public mq_notify provider.
// Every call (including cleanup) requires an admitted dispatcher Operation,
// application signals blocked and cancellation suppressed. Dispatcher intake
// already provides the operation for consume(). No table lock is held across
// thread creation, syscalls, loader calls, resource release or application code.
//
// Keep this object across dispatcher/socket restarts: cookies are never reused.
// Its registry is MQ-only; timer cookies must be routed to a different registry.
// External service admission must exclude destruction and fork/exec snapshots.
class NotificationMq {
  struct Record;
  // MMIX subword atomics require a complete containing octa, as in lifecycle.h.
  struct alignas(8) TableMutex {
    RawMutex value;
  } mutex;
  class Lock;
  NotificationRegistry registry;
  Record *cleanup_head = nullptr;
  size_t outstanding = 0;

  static void finish_waiter(Record &, bool activate);
  long cleanup(Record &);

public:
  // A nonzero watermark is only for a drained predecessor or exhaustion tests;
  // never lower the watermark within a process lifetime.
  explicit NotificationMq(uint64_t last_issued_cookie = 0)
      : registry(last_issued_cookie) {}
  ~NotificationMq();
  NotificationMq(const NotificationMq &) = delete;
  NotificationMq &operator=(const NotificationMq &) = delete;

  // Positive errno, no errno mutation. The caller has started the dispatcher
  // and enabled MQ; socket_fd is its private mq_fd(), stable throughout this
  // transaction and every resulting registration. Precreates/validates the
  // managed waiter before publishing Installing and issuing raw mq_notify.
  // Failed cleanup is retained here, never substituted for the syscall error.
  long install(int mqdes, int socket_fd, const sigevent &, const TLSImage &,
               uintptr_t page_size);

  // Only pass events validated by notification_transport::decode_mq (normally
  // from NotificationDispatcher's MQ handler). First terminal event wins;
  // unknown/duplicate/late cookies are ignored. No creation on delivery.
  // Cleanup failures are queued without stopping dispatcher intake.
  void consume(const notification_transport::Notification &);

  // Retry each currently retained cleanup once, outside the table lock.
  // Returns the first positive error; failed owners remain available for retry.
  long retry_cleanup();

  // Metadata snapshots, not a service-retirement or callback-drain witness.
  // Detached callback resources independently retain NotificationCallback until
  // terminal clear-TID/reaping, even after this table becomes empty. The global
  // service must also drain those owners and exclude in-flight Operations.
  bool drained();
  uint64_t last_issued_cookie();
};

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
