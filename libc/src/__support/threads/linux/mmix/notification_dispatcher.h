//===-- MMIX notification dispatch lifecycle --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_NOTIFICATION_DISPATCHER_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_NOTIFICATION_DISPATCHER_H

#include "src/__support/OSUtil/linux/notification_channels.h"
#include "thread_create.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// Resident runtime handlers only, never application callbacks. Each handler
// validates registry ownership before using an event. It may activate/create
// managed callbacks, but must not unwind or wait for application code. A
// positive error stops intake and is reported to the service owner; the helper
// remains alive so its published timer TID cannot be reused before stop().
struct NotificationHandlers {
  void *context = nullptr;
  int (*timer)(void *, const signalfd_siginfo &) = nullptr;
  int (*mq)(void *, const notification_transport::Notification &) = nullptr;
};

// A service-owned helper, not a public provider or an ownership registry.
// Serialize start/enable_mq/stop externally, suppress cancellation, and block
// application signals while leaving credential notification enabled. The
// external service gate must also cover fork/exec and all registration owners.
// Do not destroy or reset this object in a copied child while it is active.
// Handlers/context outlive stop(), including failed resource-release retries.
class NotificationDispatcher {
  notification_channels::Channels<> channels;
  NotificationHandlers handlers{};
  ThreadControl *control = nullptr;
  ThreadResources retained{};
  LifecycleWord ready;
  cpp::Atomic<bool> stopping{false};
  cpp::Atomic<int> failure{0};
  cpp::Atomic<int> published_mq{-1};
  static constexpr unsigned PARKED = ~0U;
  cpp::Atomic<unsigned> activity{0};

  bool enter();
  void leave();

  static void *entry(void *);
  int run();
  void publish_ready();
  int await_ready();

public:
  // Covers service work outside its table lock, including allocation, thread
  // creation and owner release. The service must hold this through the whole
  // transaction, not merely its metadata update. Never hold it across user
  // callback execution. Suppress cancellation and application signals first.
  // start/enable_mq/stop hold their own nested operation; a service must cover
  // the work surrounding those calls too. They return EAGAIN while parked.
  class Operation {
    NotificationDispatcher *dispatcher;

  public:
    explicit Operation(NotificationDispatcher &owner)
        : dispatcher(owner.enter() ? &owner : nullptr) {}
    ~Operation() {
      if (dispatcher)
        dispatcher->leave();
    }
    Operation(const Operation &) = delete;
    Operation &operator=(const Operation &) = delete;
    explicit operator bool() const { return dispatcher != nullptr; }
  };

  NotificationDispatcher() = default;
  ~NotificationDispatcher() {
    if (!drained() || activity.load(cpp::MemoryOrder::RELAXED))
      __builtin_trap();
  }
  NotificationDispatcher(const NotificationDispatcher &) = delete;
  NotificationDispatcher &operator=(const NotificationDispatcher &) = delete;

  // Nonblocking fork checkpoint: success excludes all intake handlers and
  // externally guarded transactions, without retiring the timer target TID.
  // It does not establish child recovery or retire kernel registrations. The
  // service owner serializes park/resume and holds the checkpoint through the
  // clone snapshot. A failed attempt leaves admission unchanged.
  bool try_park();
  void resume();

  // Positive errno. Success includes runner readiness, not merely clone READY.
  // A failure can retain cleanup ownership; stop() must succeed before retry.
  int start(const TLSImage &, uintptr_t page_size, NotificationHandlers);
  int enable_mq();
  // Stops intake, waits for managed terminal clear, then releases resources
  // and closes channels exactly once. Does not retire registered kernel timers
  // or MQ registrations: the owning service must do that before stopping.
  int stop();
  int error() { return failure.load(cpp::MemoryOrder::ACQUIRE); }
  int timer_tid() const { return control ? control->attributes.tid : 0; }
  int mq_fd() { return published_mq.load(cpp::MemoryOrder::ACQUIRE); }
  bool drained() const {
    return !control && !retained.owns_mappings() && channels.control_fd() < 0;
  }
};

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
