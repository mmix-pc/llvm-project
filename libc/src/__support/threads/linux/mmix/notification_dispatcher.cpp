//===-- MMIX notification dispatch lifecycle ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "notification_dispatcher.h"
#include "credentials.h"
#include "hdr/types/struct_pollfd.h"
#include "hdr/types/struct_timespec.h"
#include "include/llvm-libc-macros/poll-macros.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/signal/linux/mmix/private_signals.h"
#include "thread_reclaim.h"
#include <linux/futex.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

bool NotificationDispatcher::enter() {
  unsigned count = activity.load(cpp::MemoryOrder::ACQUIRE);
  for (;;) {
    if (count >= PARKED - 1)
      return false;
    if (activity.compare_exchange_weak(count, count + 1,
                                       cpp::MemoryOrder::ACQ_REL))
      return true;
  }
}

void NotificationDispatcher::leave() {
  unsigned previous = activity.fetch_sub(1, cpp::MemoryOrder::RELEASE);
  if (!previous || previous == PARKED)
    __builtin_trap();
}

bool NotificationDispatcher::try_park() {
  unsigned idle = 0;
  return activity.compare_exchange_strong(idle, PARKED,
                                          cpp::MemoryOrder::ACQ_REL);
}

void NotificationDispatcher::resume() {
  unsigned parked = PARKED;
  if (!activity.compare_exchange_strong(parked, 0, cpp::MemoryOrder::RELEASE))
    __builtin_trap();
}

void NotificationDispatcher::publish_ready() {
  ready.value.store(1, cpp::MemoryOrder::RELEASE);
  if (!ready.value.notify_all())
    __builtin_trap();
}

int NotificationDispatcher::await_ready() {
  while (!ready.value.load(cpp::MemoryOrder::ACQUIRE)) {
    if (!control->clear_tid.value.load(cpp::MemoryOrder::ACQUIRE))
      // GO has already committed managed entry. Terminal clear alone cannot
      // supply its missing cleanup/ExitReady transitions for normal reaping.
      __builtin_trap();
    // Check terminal clear even if a child exits before acknowledging entry.
    const timespec delay{0, 10000000};
    auto result = linux_syscalls::syscall_checked<int>(
        SYS_futex, &ready.value.val, FUTEX_WAIT_PRIVATE, 0, &delay, 0, 0);
    if (!result && result.error() != EINTR && result.error() != EAGAIN &&
        result.error() != ETIMEDOUT)
      return result.error();
  }
  return error();
}

int NotificationDispatcher::start(const TLSImage &image, uintptr_t page_size,
                                  NotificationHandlers input) {
  Operation operation(*this);
  if (!operation)
    return EAGAIN;
  if (!drained())
    return EBUSY;
  if (!input.timer || !input.mq || page_size != 8192)
    return EINVAL;
#ifndef LIBC_MMIX_PTHREAD_SCHEDULING
  // Do not inherit an arbitrary real-time policy from the first registrant.
  return ENOTSUP;
#else
  handlers = input;
  stopping.store(false, cpp::MemoryOrder::RELAXED);
  failure.store(0, cpp::MemoryOrder::RELAXED);
  ready.value.store(0, cpp::MemoryOrder::RELAXED);
  auto initialized = channels.initialize(NOTIFICATION_SIGNAL);
  if (!initialized)
    return initialized.error();
  ThreadPreparation request;
  request.image = image;
  request.page_size = page_size;
  request.detached = true;
  request.apply_scheduling = true;
  request.scheduling_policy = 0; // Linux SCHED_OTHER, priority zero.
  request.scheduling_priority = 0;
  request.runner.posix_runner = entry;
  request.argument = this;
  ThreadAttributes *attributes = nullptr;
  auto created = create_thread_helper(request, attributes, &control);
  if (created.error) {
    retained = created.preparation.retained;
    (void)channels.reset();
    return int(-created.error);
  }
  int result = await_ready();
  if (result)
    (void)stop();
  return result;
#endif
}

int NotificationDispatcher::enable_mq() {
  Operation operation(*this);
  if (!operation)
    return EAGAIN;
  if (!control || stopping.load(cpp::MemoryOrder::ACQUIRE))
    return EINVAL;
  if (int result = error())
    return result;
  auto opened = channels.ensure_mq();
  if (!opened)
    return opened.error();
  // The reader acquires this publication before reading Channels' MQ fd.
  published_mq.store(channels.mq_fd(), cpp::MemoryOrder::RELEASE);
  auto woke = channels.wake();
  return woke ? 0 : woke.error();
}

int NotificationDispatcher::stop() {
  Operation operation(*this);
  if (!operation)
    return EAGAIN;
  stopping.store(true, cpp::MemoryOrder::RELEASE);
  int result = 0;
  if (control) {
    auto woke = channels.wake();
    if (!woke)
      result = woke.error();
    // A bounded poll interval also observes stop if its wake failed. Only the
    // exclusive owner reclaims this helper, after kernel clear and pin drain.
    long released = reclaim_thread(*control, ThreadOwner::Detached, retained);
    control = nullptr;
    if (released && !result)
      result = int(released);
  } else if (retained.owns_mappings()) {
    result = int(release_thread_resources(retained));
  }
  published_mq.store(-1, cpp::MemoryOrder::RELEASE);
  auto closed = channels.reset();
  if (!closed && !result)
    result = closed.error();
  return result;
}

void *NotificationDispatcher::entry(void *argument) {
  auto &dispatcher = *static_cast<NotificationDispatcher *>(argument);
  uint64_t mask = 0;
  auto queried = linux_syscalls::syscall_checked<int>(
      SYS_rt_sigprocmask, SIG_SETMASK, 0, &mask, sizeof(mask));
  int result = queried ? 0 : queried.error();
  if (!result &&
      (!(mask & notification_signal_mask) || (mask & CREDENTIAL_MASK)))
    result = EPROTO;
  dispatcher.failure.store(result, cpp::MemoryOrder::RELEASE);
  dispatcher.publish_ready();
  if (!result)
    result = dispatcher.run();
  dispatcher.failure.store(result, cpp::MemoryOrder::RELEASE);
  // A published timer target must not disappear and allow its TID to be
  // recycled while an owner is registering a notification. Even failed intake
  // retains the kernel task until coordinated stop, without executing events.
  while (!dispatcher.stopping.load(cpp::MemoryOrder::ACQUIRE)) {
    const timespec delay{0, 100000000};
    auto waited = linux_syscalls::syscall_checked<int>(
        SYS_futex, &dispatcher.ready.value.val, FUTEX_WAIT_PRIVATE, 1, &delay,
        0, 0);
    if (!waited && waited.error() != EINTR && waited.error() != EAGAIN &&
        waited.error() != ETIMEDOUT)
      __builtin_trap();
  }
  return nullptr;
}

int NotificationDispatcher::run() {
  while (!stopping.load(cpp::MemoryOrder::ACQUIRE)) {
    pollfd descriptors[3] = {
        {channels.control_fd(), POLLIN, 0},
        {channels.timer_fd(), POLLIN, 0},
        {published_mq.load(cpp::MemoryOrder::ACQUIRE), POLLIN, 0}};
    const timespec interval{0, 100000000};
    auto polled = linux_syscalls::syscall_checked<int>(
        SYS_ppoll, descriptors, 3, &interval, 0, sizeof(uint64_t));
    if (!polled) {
      if (polled.error() == EINTR)
        continue;
      return polled.error();
    }
    // Polling observes readiness but consumes no event. Claim intake before
    // reading; fork can park an idle helper even while it is polling. A ready
    // descriptor must not turn a parked helper into a busy loop.
    Operation intake(*this);
    if (!intake) {
      const timespec delay{0, 100000000};
      auto waited = linux_syscalls::syscall_checked<int>(
          SYS_futex, &ready.value.val, FUTEX_WAIT_PRIVATE, 1, &delay, 0, 0);
      if (!waited && waited.error() != EINTR && waited.error() != EAGAIN &&
          waited.error() != ETIMEDOUT)
        return waited.error();
      continue;
    }
    for (unsigned index = 0; index < 3; ++index) {
      if (stopping.load(cpp::MemoryOrder::ACQUIRE))
        return 0;
      if (descriptors[index].revents & POLLNVAL)
        return EBADF;
      if (descriptors[index].revents & (POLLERR | POLLHUP))
        return EIO;
      if (!(descriptors[index].revents & POLLIN))
        continue;
      int result = 0;
      if (index == 0) {
        uint64_t count;
        auto read = channels.read_control(count);
        result = read ? 0 : read.error();
      } else if (index == 1) {
        signalfd_siginfo info;
        auto read = channels.read_timer(info);
        if (!read) {
          result = read.error();
        } else {
          result = handlers.timer(handlers.context, info);
          if (result)
            return result;
        }
      } else {
        notification_transport::Notification event;
        auto read = channels.read_mq(event);
        if (!read) {
          result = read.error();
        } else {
          result = handlers.mq(handlers.context, event);
          if (result)
            return result;
        }
      }
      if (result && result != EAGAIN && result != EINTR)
        return result;
    }
  }
  return 0;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
