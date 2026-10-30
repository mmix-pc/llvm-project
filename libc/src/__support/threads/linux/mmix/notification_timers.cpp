//===-- MMIX managed timer registrations ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "notification_timers.h"
#include "credentials.h"
#include "thread_create.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/new.h"
#include "src/__support/OSUtil/linux/kernel_sigevent.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/timer_delete.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/timer_settime.h"
#include "src/signal/linux/mmix/private_signals.h"
#include "src/stdlib/free.h"
#include "src/stdlib/malloc.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {
class Lock {
  RawMutex &mutex;

public:
  explicit Lock(RawMutex &mutex) : mutex(mutex) { mutex.lock(); }
  ~Lock() {
    if (!mutex.unlock())
      __builtin_trap();
  }
};
} // namespace

void NotificationTimers::destroy(Record *record) {
  if (!record->reclaimable())
    __builtin_trap();
  // A failed final image release retains ownership; never free its owner or
  // continue with an untracked callback pin. Match thread rollback containment.
  if (record->callback->release())
    __builtin_trap();
  record->~Record();
  LIBC_NAMESPACE::free(record);
}

int NotificationTimers::create(clockid_t clock, const sigevent &event,
                               const TLSImage &image, uintptr_t page_size,
                               int helper_tid, timer_t &output) {
  if (helper_tid <= 0)
    return EINVAL;
  NotificationCallback *callback = nullptr;
  if (long error =
          NotificationCallback::create(event, image, page_size, callback))
    return int(error);
  void *storage = LIBC_NAMESPACE::malloc(sizeof(Record));
  if (!storage) {
    if (callback->release())
      __builtin_trap();
    return ENOMEM;
  }
  auto *record = new (storage) Record(callback);
  int error = 0;
  {
    Lock lock(mutex.value);
    if (!registry.install(*record)) {
      error = EOVERFLOW;
    } else {
      linux_syscalls::KernelSigevent kernel{};
      kernel.sigev_notify = SIGEV_SIGNAL | SIGEV_THREAD_ID;
      kernel.sigev_signo = NOTIFICATION_SIGNAL;
      kernel.sigev_value.sival_ptr =
          reinterpret_cast<void *>(record->cookie());
      kernel._sigev_un._tid = helper_tid;
      auto created = linux_syscalls::syscall_checked<int>(
          SYS_timer_create, clock, &kernel, &record->id);
      if (!created) {
        error = created.error();
        if (!registry.retire(*record))
          __builtin_trap();
      } else {
        // New kernel timers are disarmed. Publish before exposing their ID.
        if (record->id < 0 || !registry.publish(*record))
          __builtin_trap();
        record->next = head;
        head = record;
        record->signal_next.store(signal_head.load());
        signal_head.store(record);
        output = reinterpret_cast<timer_t>(static_cast<long>(record->id));
      }
    }
  }
  if (error)
    destroy(record);
  (void)collect_retired();
  return error;
}

int NotificationTimers::remove(timer_t timer) {
  {
    Lock lock(mutex.value);
    auto removed = linux_syscalls::timer_delete(timer);
    if (!removed)
      return removed.error();
    const int id = static_cast<int>(reinterpret_cast<long>(timer));
    for (Record **link = &head; *link; link = &(*link)->next) {
      Record *record = *link;
      if (record->id != id)
        continue;
      *link = record->next;
      auto *signal_link = &signal_head;
      while (signal_link->load() != record)
        signal_link = &signal_link->load()->signal_next;
      signal_link->store(record->signal_next.load());
      if (!registry.retire(*record))
        __builtin_trap();
      if (record->reclaimable()) {
        record->retired_next = retired;
        retired = record;
      }
      break;
    }
  }
  (void)collect_retired();
  return 0;
}

int NotificationTimers::settime(timer_t timer, int flags,
                                const itimerspec *value, itimerspec *old) {
  // A handler may siglongjmp out of an interrupted call. Keep application
  // signals and cancellation blocked until the reader reference is released;
  // credential coordination stays enabled and cannot invoke application code.
  uint64_t saved_mask;
  uint64_t blocked = UINT64_MAX & ~CREDENTIAL_MASK;
  auto masked = linux_syscalls::syscall_checked<int>(
      SYS_rt_sigprocmask, SIG_SETMASK, &blocked, &saved_mask, sizeof(saved_mask));
  if (!masked)
    return masked.error();
  // Sequential consistency orders reader admission against unlink and the
  // zero-reader reclamation check. Readers admitted after unlink cannot reach
  // a retired node; earlier readers keep both record and callback storage live.
  if (signal_readers.fetch_add(1) == UINT64_MAX)
    __builtin_trap();
  const int id = static_cast<int>(reinterpret_cast<long>(timer));
  int error = 0;
  if (value && (value->it_interval.tv_sec || value->it_interval.tv_nsec))
    for (Record *record = signal_head.load(); record;
         record = record->signal_next.load())
      if (record->id == id && record->callback->supplied_stack()) {
        error = EINVAL;
        break;
      }
  if (!error) {
    auto changed = linux_syscalls::timer_settime(timer, flags, value, old);
    error = changed ? 0 : changed.error();
  }
  if (!signal_readers.fetch_sub(1))
    __builtin_trap();
  auto restored = linux_syscalls::syscall_checked<int>(
      SYS_rt_sigprocmask, SIG_SETMASK, &saved_mask, nullptr, sizeof(saved_mask));
  if (!restored)
    __builtin_trap();
  return error;
}

void NotificationTimers::dispatch(const signalfd_siginfo &info) {
  NotificationRegistry::EventReference event;
  Record *record;
  {
    Lock lock(mutex.value);
    if (!registry.acquire(info.ssi_ptr, event))
      return; // An unknown or retired cookie is a discardable late event.
    record = static_cast<Record *>(event.get());
    notification_transport::Notification decoded;
    if (!notification_transport::decode_timer(
            &info, sizeof(info), NOTIFICATION_SIGNAL, record->id, decoded)) {
      if (!registry.release(event))
        __builtin_trap();
      return;
    }
  }
  // The event owns the registration until creation has retained its own
  // resource reference. Deletion may complete meanwhile, including from the
  // newly launched callback. Never clone or release loader pins under the gate.
  ThreadAttributes *attributes = nullptr;
  auto result = create_thread(record->callback->thread_preparation(), attributes);
  if (result.preparation.retained.owns_mappings())
    __builtin_trap();
  {
    Lock lock(mutex.value);
    if (!registry.release(event))
      __builtin_trap();
    if (record->reclaimable()) {
      record->retired_next = retired;
      retired = record;
    }
  }
  (void)collect_retired();
  // Launch errors intentionally drop only this event. Later periodic events
  // get independent attempts; the registration itself remains live.
}

bool NotificationTimers::collect_retired() {
  Record *pending;
  {
    Lock lock(mutex.value);
    if (signal_readers.load())
      return false;
    pending = retired;
    retired = nullptr;
  }
  while (pending) {
    Record *record = pending;
    pending = record->retired_next;
    destroy(record);
  }
  return true;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
