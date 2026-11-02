//===-- MMIX process notification service --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "notification_service.h"
#include "cancellation.h"
#include "config/app.h"
#include "credentials.h"
#include "hdr/pthread_macros.h"
#include "notification_dispatcher.h"
#include "notification_mq.h"
#include "notification_timers.h"
#include "src/__support/CPP/new.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/timer_delete.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/timer_settime.h"
#include <asm/rstack.h>
#include <asm/unistd.h>
#include <pthread.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {
struct Service {
  NotificationDispatcher dispatcher;
  NotificationTimers timers;
  NotificationMq mq;
};
// Permanent process storage deliberately has no exit destructor. Active
// helpers and copied-child quarantine are retired by exec/exit_group, never by
// waiting from an atexit handler or releasing vanished-thread loader tickets.
alignas(Service) unsigned char storage[sizeof(Service)];
Service *service = nullptr;
LIBC_CONSTINIT cpp::Atomic<Service *> signal_service{nullptr};
struct alignas(8) Serializer { RawMutex mutex; };
LIBC_CONSTINIT Serializer serializer;
LIBC_CONSTINIT cpp::Atomic<uint64_t> closed{0};
bool quarantined = false;
bool starting = false;
bool checkpoint = false;

void maintain(void *context) {
  auto &owner = *static_cast<Service *>(context);
  (void)owner.timers.collect_retired();
  (void)owner.mq.retry_cleanup();
}

int timer_event(void *context, const signalfd_siginfo &event) {
  if (!closed.load(cpp::MemoryOrder::ACQUIRE))
    static_cast<Service *>(context)->timers.dispatch(event);
  return 0;
}
int mq_event(void *context,
             const notification_transport::Notification &event) {
  // Activation itself participates in the registry's process-exit election.
  static_cast<Service *>(context)->mq.consume(event);
  return 0;
}

// Serialize state publication only. Loader holds and thread creation must run
// outside this gate: a DSO constructor may itself register a notification.
class Call {
  uint64_t saved_mask = 0;
  int saved_cancel = PTHREAD_CANCEL_DISABLE;
  bool masked = false, disabled = false, locked = false;
  int error_code = 0;

public:
  explicit Call(bool creating = true) {
    auto *caller = current_control;
    if (!caller || (creating &&
                   !caller->fork_recovery.allows_thread_creation())) {
      error_code = ENOTSUP;
      return;
    }
    uint64_t blocked = UINT64_MAX & ~CREDENTIAL_MASK;
    long error = syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK, &blocked,
                                    &saved_mask, sizeof(saved_mask));
    if (error) {
      error_code = int(-error);
      return;
    }
    masked = true;
    if (creating) {
      mmix_rstack_query query{};
      error = syscall_impl<long>(__NR_mmix_rstack_query, &query);
      if (error || !caller->unwind_root.active ||
          query.chain_id != caller->unwind_root.chain) {
        error_code = error ? int(-error) : ENOTSUP;
        return;
      }
    }
    error_code = set_cancel_state(PTHREAD_CANCEL_DISABLE, &saved_cancel);
    if (error_code)
      return;
    disabled = true;
    serializer.mutex.lock();
    locked = true;
    if (quarantined || checkpoint || (creating && starting) ||
        closed.load(cpp::MemoryOrder::ACQUIRE))
      error_code = EAGAIN;
  }
  ~Call() noexcept(false) {
    if (locked && !serializer.mutex.unlock())
      __builtin_trap();
    // Restore the mask while cancellation is still disabled. Re-enabling may
    // deliver a pending request only after all service owners/locks are safe.
    if (masked && syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK,
                                     &saved_mask, nullptr, sizeof(saved_mask)))
      __builtin_trap();
    if (disabled && set_cancel_state(saved_cancel, nullptr))
      __builtin_trap();
  }
  int error() const { return error_code; }
  void unlock() {
    if (!locked || !serializer.mutex.unlock())
      __builtin_trap();
    locked = false;
  }
  void lock() {
    serializer.mutex.lock();
    locked = true;
  }
};

int ensure_service(Call &call) {
  if (!service) {
    service = new (storage) Service;
    signal_service.store(service, cpp::MemoryOrder::RELEASE);
  }
  auto &dispatcher = service->dispatcher;
  if (dispatcher.timer_tid())
    return dispatcher.error();
  NotificationDispatcher::Operation operation(dispatcher);
  if (!operation)
    return EAGAIN;
  starting = true;
  call.unlock();
  int error = 0;
  if (!dispatcher.drained()) {
    error = dispatcher.stop();
  }
  if (!error)
    error = dispatcher.start(app.tls, app.page_size,
                             {service, timer_event, mq_event, maintain});
  call.lock();
  starting = false;
  return error;
}
} // namespace

int create_notification_timer(clockid_t clock, const sigevent &event,
                               timer_t &output) {
  Call call;
  if (call.error())
    return call.error();
  if (int error = ensure_service(call))
    return error;
  NotificationDispatcher::Operation operation(service->dispatcher);
  if (!operation)
    return EAGAIN;
  call.unlock();
  return service->timers.create(clock, event, app.tls, app.page_size,
                                service->dispatcher.timer_tid(), output);
}

int delete_notification_timer(timer_t timer) {
  if (!signal_service.load(cpp::MemoryOrder::ACQUIRE)) {
    auto result = linux_syscalls::timer_delete(timer);
    return result ? 0 : result.error();
  }
  Call call(false);
  if (call.error())
    return call.error();
  if (!service) {
    auto result = linux_syscalls::timer_delete(timer);
    return result ? 0 : result.error();
  }
  NotificationDispatcher::Operation operation(service->dispatcher);
  call.unlock();
  return operation ? service->timers.remove(timer) : EAGAIN;
}

int set_notification_timer(timer_t timer, int flags, const itimerspec *value,
                            itimerspec *old) {
  // timer_settime is signal-safe, including when it interrupts a registration
  // transaction. The permanent table offers a separate lock-free read path.
  auto *published = signal_service.load(cpp::MemoryOrder::ACQUIRE);
  if (!published) {
    auto result = linux_syscalls::timer_settime(timer, flags, value, old);
    return result ? 0 : result.error();
  }
  return published->timers.settime(timer, flags, value, old);
}

int register_mq_notification(int descriptor, const sigevent &event) {
  Call call;
  if (call.error())
    return call.error();
  if (int error = ensure_service(call))
    return error;
  NotificationDispatcher::Operation operation(service->dispatcher);
  if (!operation)
    return EAGAIN;
  if (int error = service->dispatcher.enable_mq())
    return error;
  call.unlock();
  (void)service->mq.retry_cleanup();
  return int(service->mq.install(descriptor, service->dispatcher.mq_fd(), event,
                                 app.tls, app.page_size));
}

int prepare_notification_fork(bool minimal, bool *drained, bool retire_helpers) {
  *drained = false;
  if (!serializer.mutex.try_lock())
    return EAGAIN;
  if (starting || checkpoint || quarantined) {
    serializer.mutex.unlock();
    return EAGAIN;
  }
  if (!service) {
    checkpoint = true;
    serializer.mutex.unlock();
    *drained = true;
    return 0;
  }
  auto &dispatcher = service->dispatcher;
  if (!dispatcher.try_park()) {
    serializer.mutex.unlock();
    return EAGAIN;
  }
  checkpoint = true;
  serializer.mutex.unlock();
  // Retirement releases loader owners. Publish the checkpoint instead of
  // holding a mutex across that release or the copied-process snapshot.
  if (retire_helpers && service->timers.empty() && service->mq.drained()) {
    int error = !service->timers.collect_retired() ||
                        (minimal && !dispatcher.drained())
                    ? EAGAIN
                    : dispatcher.stop_parked();
    if (error) {
      serializer.mutex.lock();
      dispatcher.resume();
      checkpoint = false;
      serializer.mutex.unlock();
      return error;
    }
  }
  *drained = dispatcher.drained();
  return 0;
}

void finish_notification_fork(bool child) {
  serializer.mutex.lock();
  if (!checkpoint)
    __builtin_trap();
  if (service) {
    if (child && !service->dispatcher.drained()) {
      service->dispatcher.quarantine_parked_child();
      signal_service.store(nullptr, cpp::MemoryOrder::RELAXED);
      quarantined = true;
    } else {
      service->dispatcher.resume();
    }
  }
  if (child)
    closed.store(0, cpp::MemoryOrder::RELAXED);
  checkpoint = false;
  if (!serializer.mutex.unlock())
    __builtin_trap();
}

void close_notification_admission() {
  closed.store(1, cpp::MemoryOrder::RELEASE);
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
