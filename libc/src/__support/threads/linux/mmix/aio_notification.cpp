//===-- MMIX precreated AIO notification ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "aio_notification.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "src/signal/linux/mmix/private_signals.h"
#include "thread_create.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

long AioNotification::retain_owner(void *pointer) {
  auto &owner = *static_cast<AioNotification *>(pointer);
  uint64_t references =
      owner.resource_references.load(cpp::MemoryOrder::RELAXED);
  for (;;) {
    if (references == UINT64_MAX)
      return EAGAIN;
    if (owner.resource_references.compare_exchange_weak(
            references, references + 1, cpp::MemoryOrder::ACQ_REL,
            cpp::MemoryOrder::RELAXED))
      return 0;
  }
}

long AioNotification::release_owner(void *pointer) {
  auto &owner = *static_cast<AioNotification *>(pointer);
  if (!owner.resource_references.fetch_sub(1, cpp::MemoryOrder::RELEASE))
    __builtin_trap();
  return 0;
}

AioNotification::~AioNotification() {
  if (!drained())
    __builtin_trap();
}

long AioNotification::prepare(const sigevent &event, const TLSImage &image,
                              uintptr_t page_size) {
  if (state != State::Fresh)
    return EALREADY;
  state = State::Terminal;
  mode = event.sigev_notify;
  if (mode == SIGEV_NONE) {
    state = State::Prepared;
    return 0;
  }
  if (mode == SIGEV_SIGNAL) {
    if (event.sigev_signo <= 0 || event.sigev_signo >= NSIG ||
        private_signal(event.sigev_signo))
      return EINVAL;
    signal_info.si_signo = event.sigev_signo;
    signal_info.si_code = SI_ASYNCIO;
    signal_info.si_value = event.sigev_value;
    long pid = syscall_impl(SYS_getpid);
    if (pid < 0)
      return -pid;
    long uid = syscall_impl(SYS_getuid);
    if (uid < 0)
      return -uid;
    signal_info.si_pid = static_cast<pid_t>(pid);
    signal_info.si_uid = static_cast<uid_t>(uid);
  } else if (mode == SIGEV_THREAD) {
    long error =
        NotificationCallback::create(event, image, page_size, callback);
    if (error)
      return error;
    ThreadPreparation preparation = callback->thread_preparation();
    // The callback and this stable completion owner survive even after the
    // application consumes the I/O result. Reaping, not activation, releases
    // the last notification reference held by the service.
    preparation.resource_owner = {this, retain_owner, release_owner};
    ThreadCreationResult created = create_callback_waiter(preparation, waiter);
    if (created.error) {
      retained = created.preparation.retained;
      // Keep the primary preparation error. Cleanup has a separate retry API.
      return -created.error;
    }
  } else {
    return EINVAL;
  }
  state = State::Prepared;
  return 0;
}

long AioNotification::activate() {
  if (state != State::Prepared)
    return state == State::Fresh ? EINVAL : EALREADY;
  if (mode == SIGEV_THREAD) {
    ThreadRegistryLock lock(thread_registry);
    if (!lock.activate_callback(*waiter))
      return EBUSY;
    ThreadControl *control = waiter;
    waiter = nullptr;
    state = State::Terminal;
    if (!lock.drop_creator(*control))
      __builtin_trap();
    // No control access after dropping the pin; registry unlock wakes entry.
    return 0;
  }
  if (mode == SIGEV_NONE) {
    state = State::Terminal;
    return 0;
  }
  // Every attempt, including queue-pressure retries, shares the process-exit
  // election boundary. An already admitted syscall may finish after election;
  // exit does not wait for it or undo signals already queued.
  if (!thread_registry.admit_signal_notification())
    return EBUSY;
  // Do not accidentally signal the parent if service child-reset was omitted.
  long pid = syscall_impl(SYS_getpid);
  if (pid < 0) {
    state = State::Terminal;
    return -pid;
  }
  if (pid != signal_info.si_pid) {
    state = State::Terminal;
    return ESRCH;
  }
  long result = syscall_impl(SYS_rt_sigqueueinfo, pid, signal_info.si_signo,
                             reinterpret_cast<long>(&signal_info));
  if (result > 0)
    __builtin_trap();
  // Queue exhaustion did not deliver the signal. Preserve the copied event
  // for a later service iteration; never change the original I/O result.
  if (result == -EAGAIN)
    return EAGAIN;
  state = State::Terminal;
  return result < 0 ? -result : 0;
}

void AioNotification::discard() {
  state = State::Terminal;
  if (!waiter)
    return;
  ThreadControl *control = waiter;
  waiter = nullptr;
  ThreadRegistryLock lock(thread_registry);
  if (!lock.discard_callback(*control) || !lock.drop_creator(*control))
    __builtin_trap();
}

long AioNotification::cleanup() {
  if (state == State::Prepared)
    return EBUSY;
  long error = 0;
  if (retained.owns_mappings())
    error = release_thread_resources(retained);
  if (resource_references.load(cpp::MemoryOrder::ACQUIRE))
    return error ? error : EBUSY;
  if (callback) {
    long released = callback->release();
    if (!released)
      callback = nullptr;
    else if (!error)
      error = released;
  }
  return error;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
