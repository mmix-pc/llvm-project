//===-- MMIX private MQ notification transactions --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "notification_mq.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/new.h"
#include "src/__support/OSUtil/linux/kernel_sigevent.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "src/stdlib/free.h"
#include "src/stdlib/malloc.h"
#include "thread_create.h"
#include "thread_reclaim.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

struct NotificationMq::Record : NotificationRegistry::Node {
  enum class Terminal { None, Woken, Removed };
  Terminal terminal = Terminal::None;
  NotificationCallback *callback = nullptr;
  ThreadControl *waiter = nullptr;
  ThreadResources retained{};
  Record *cleanup_next = nullptr;
};

class NotificationMq::Lock {
  NotificationMq &owner;

public:
  explicit Lock(NotificationMq &owner) : owner(owner) {
    owner.mutex.value.lock();
  }
  ~Lock() { owner.mutex.value.unlock(); }
  Lock(const Lock &) = delete;
  Lock &operator=(const Lock &) = delete;
};

NotificationMq::~NotificationMq() {
  if (outstanding || cleanup_head)
    __builtin_trap();
}

void NotificationMq::finish_waiter(Record &record, bool activate,
                                   bool failed_registration) {
  if (failed_registration && activate)
    __builtin_trap();
  ThreadControl *control = record.waiter;
  record.waiter = nullptr;
  if (!control)
    return;
  // No table lock here. Activation and application-lifetime admission are one
  // registry operation. Failure to admit must still terminate the dormant task.
  bool await_exit = failed_registration && record.callback->supplied_stack();
  {
    ThreadRegistryLock lock(thread_registry);
    if ((!activate || !lock.activate_callback(*control)) &&
        !lock.discard_callback(*control))
      __builtin_trap();
    if (!await_exit && !lock.drop_creator(*control))
      __builtin_trap();
  }
  if (await_exit) {
    // The creator pin prevents reaping while clear_tid is observed unlocked.
    await_thread_clear(*control);
    ThreadRegistryLock lock(thread_registry);
    if (!lock.drop_creator(*control))
      __builtin_trap();
  }
  // Last control access above. Unlock publishes/wakes the process-lifetime
  // registry event; it must not notify through potentially reclaimed control.
}

long NotificationMq::cleanup(Record &record) {
  if (!record.reclaimable() || record.waiter)
    __builtin_trap();
  long error = 0;
  if (record.retained.owns_mappings())
    error = release_thread_resources(record.retained);
  if (record.callback) {
    long released = record.callback->release();
    if (!released)
      record.callback = nullptr;
    else if (!error)
      error = released;
  }
  if (error) {
    Lock lock(*this);
    record.cleanup_next = cleanup_head;
    cleanup_head = &record;
    return error;
  }
  record.~Record();
  LIBC_NAMESPACE::free(&record);
  Lock lock(*this);
  --outstanding;
  return 0;
}

long NotificationMq::install(int mqdes, int socket_fd, const sigevent &event,
                             const TLSImage &image, uintptr_t page_size) {
  if (socket_fd < 0)
    return EINVAL;
  void *storage = LIBC_NAMESPACE::malloc(sizeof(Record));
  if (!storage)
    return ENOMEM;
  auto *record = new (storage) Record;
  bool admitted;
  {
    Lock lock(*this);
    admitted = outstanding != SIZE_MAX;
    if (admitted)
      ++outstanding;
  }
  if (!admitted) {
    record->~Record();
    LIBC_NAMESPACE::free(record);
    return EOVERFLOW;
  }
  long error = NotificationCallback::create(event, image, page_size,
                                            record->callback);
  if (error) {
    cleanup(*record);
    return error;
  }
  ThreadCreationResult created = create_callback_waiter(
      record->callback->thread_preparation(), record->waiter);
  if (created.error) {
    record->retained = created.preparation.retained;
    cleanup(*record);
    return -created.error;
  }

  unsigned char cookie[notification_transport::MQ_COOKIE_SIZE]{};
  {
    Lock lock(*this);
    admitted = registry.install(*record);
    if (admitted && !notification_transport::encode_mq_cookie(record->cookie(),
                                                             cookie))
      __builtin_trap();
  }
  if (!admitted) {
    finish_waiter(*record, false, true);
    cleanup(*record);
    return EOVERFLOW;
  }

  linux_syscalls::KernelSigevent kernel{};
  kernel.sigev_notify = SIGEV_THREAD;
  kernel.sigev_signo = socket_fd;
  kernel.sigev_value.sival_ptr = cookie;
  long result = syscall_impl(SYS_mq_notify, mqdes,
                            reinterpret_cast<long>(&kernel));
  if (result > 0)
    __builtin_trap();
  bool terminal;
  {
    Lock lock(*this);
    terminal = result < 0 || record->terminal != Record::Terminal::None;
    if (terminal) {
      if (!registry.retire(*record))
        __builtin_trap();
    } else if (!registry.publish(*record)) {
      __builtin_trap();
    }
  }
  // A published record can already have been consumed/freed. Only the terminal
  // path retains exclusive ownership past the commit lock.
  if (terminal) {
    finish_waiter(*record,
                  result == 0 && record->terminal == Record::Terminal::Woken,
                  result < 0);
    cleanup(*record);
  }
  return result < 0 ? -result : 0;
}

void NotificationMq::consume(
    const notification_transport::Notification &notification) {
  using notification_transport::Event;
  if ((notification.event != Event::MqWokenUp &&
       notification.event != Event::MqRemoved) || notification.overrun != 0)
    return;
  Record *record;
  {
    Lock lock(*this);
    NotificationRegistry::EventReference reference;
    if (!registry.acquire(notification.token, reference))
      return;
    record = static_cast<Record *>(reference.get());
    bool first = record->terminal == Record::Terminal::None;
    if (first)
      record->terminal = notification.event == Event::MqWokenUp
                             ? Record::Terminal::Woken
                             : Record::Terminal::Removed;
    bool installing = record->installing();
    if (first && !installing && !registry.retire(*record))
      __builtin_trap();
    if (!registry.release(reference))
      __builtin_trap();
    // Installing keeps the registration owner until the syscall's outcome.
    // No retained pointer/reference to the intake buffer or caller cookie.
    if (installing || !first)
      return;
  }
  finish_waiter(*record, record->terminal == Record::Terminal::Woken);
  cleanup(*record);
}

long NotificationMq::retry_cleanup() {
  Record *pending;
  {
    Lock lock(*this);
    pending = cleanup_head;
    cleanup_head = nullptr;
  }
  long error = 0;
  while (pending) {
    Record *record = pending;
    pending = record->cleanup_next;
    record->cleanup_next = nullptr;
    long released = cleanup(*record);
    if (!error)
      error = released;
  }
  return error;
}

bool NotificationMq::drained() {
  Lock lock(*this);
  return outstanding == 0;
}

uint64_t NotificationMq::last_issued_cookie() {
  Lock lock(*this);
  return registry.last_issued_cookie();
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
