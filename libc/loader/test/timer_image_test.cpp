//===-- Managed timer and real loader image ownership composition ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "timer_image_test.h"
#include "src/__support/OSUtil/linux/kernel_sigevent.h"
#include "src/__support/threads/linux/mmix/notification_timers.h"
#include "src/__support/threads/linux/mmix/thread_create.h"
#include "src/signal/linux/mmix/private_signals.h"
#include <linux/futex.h>
#include <sys/syscall.h>

using namespace timer_image_test;
using namespace timer_image_test::mmix;
namespace {
void check(bool value) {
  if (!value)
    __builtin_trap();
}
unsigned failure, allocations, frees, creates, deletes, retains, releases;
uint64_t cookie;
TimerImageTestAPI api;
NotificationTimers *table;
constexpr int timer_id = 7;
} // namespace

namespace timer_image_test {
void *malloc(size_t size) {
  ++allocations;
  if (failure <= 2 && allocations == failure)
    return nullptr;
  return timer_image_allocate(size);
}
void free(void *pointer) {
  check(pointer);
  ++frees;
  timer_image_free(pointer);
}
namespace mmix {
ThreadCreationResult create_thread(const ThreadPreparation &,
                                   ThreadAttributes *&) {
  // Registration and deletion must not launch or execute a callback.
  __builtin_trap();
}
long retain_shared_tls_destructor(void *dso, uintptr_t address,
                                  uint64_t *token) {
  check(!dso && !*token && table->empty() && table->collect_retired());
  ++retains;
  return api.retain(api.context, address, token);
}
long release_shared_tls_destructor(uint64_t token) {
  // Reentering the table also checks that destruction is outside its gate.
  check(token && table->empty() && table->collect_retired());
  ++releases;
  return api.release(api.context, token);
}
} // namespace mmix
} // namespace timer_image_test

extern "C" long __llvm_libc_mmix_syscall(unsigned long number, long a, long b,
                                         long c, long, long, long) {
  if (number == SYS_futex) {
    check(b == FUTEX_WAKE_PRIVATE); // No contended wait is expected.
    return 0;
  }
  if (number == SYS_timer_create) {
    ++creates;
    const auto &event = *reinterpret_cast<linux_syscalls::KernelSigevent *>(b);
    check(event.sigev_notify == (SIGEV_SIGNAL | SIGEV_THREAD_ID));
    check(event.sigev_signo == NOTIFICATION_SIGNAL &&
          event._sigev_un._tid == 42);
    uint64_t next = reinterpret_cast<uint64_t>(event.sigev_value.sival_ptr);
    check(next > cookie);
    cookie = next;
    if (failure == 3 && creates == 1)
      return -EAGAIN;
    *reinterpret_cast<int *>(c) = timer_id;
    return 0;
  }
  if (number == SYS_timer_delete) {
    ++deletes;
    check(a == timer_id);
    return 0;
  }
  __builtin_trap();
}

extern "C" void check_timer_image_composition(TimerImageTestAPI input,
                                              uintptr_t address) {
  api = input;
  for (unsigned selected = 1; selected <= 3; ++selected) {
    failure = selected;
    allocations = frees = creates = deletes = retains = releases = 0;
    cookie = 0;
    NotificationTimers timers;
    table = &timers;
    sigevent event{};
    event.sigev_notify = SIGEV_THREAD;
    // Real mapped MMIX code address: retain it, never execute it on the host.
    event.sigev_notify_function = reinterpret_cast<void (*)(sigval)>(address);
    timer_t id = reinterpret_cast<timer_t>(long(1234));
    check(timers.create(0, event, {}, 8192, 42, id) ==
          (selected == 3 ? EAGAIN : ENOMEM));
    check(id == reinterpret_cast<timer_t>(long(1234)) && timers.empty());
    check(frees == (selected == 1 ? 0u : selected == 2 ? 1u : 2u));
    check(retains == (selected == 1 ? 0u : 1u) && releases == retains);
    api.observe(api.context, 0);
    failure = 0;
    check(!timers.create(0, event, {}, 8192, 42, id));
    check(!timers.empty() && retains == releases + 1);
    api.observe(api.context, 1);
    check(!timers.remove(id) && timers.empty());
    check(retains == releases &&
          frees == allocations - (selected <= 2 ? 1 : 0));
    check(deletes == 1);
    api.observe(api.context, 0);
  }
  table = nullptr;
}
