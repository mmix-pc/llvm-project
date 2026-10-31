//===-- MMIX AIO completion dispatch lifecycle ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "aio_dispatcher.h"
#include "credentials.h"
#include "hdr/signal_macros.h"
#include "thread_reclaim.h"
#include <linux/futex.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {
[[noreturn]] void dispatch_failure() {
  // A broken completion channel cannot authorize releasing live I/O buffers.
  (void)syscall_impl<long>(SYS_exit_group, 127);
  __builtin_trap();
}
} // namespace

bool AioDispatcher::enter() {
  uint64_t count = activity.load(cpp::MemoryOrder::ACQUIRE);
  for (;;) {
    if (count >= PARKED - 1)
      return false;
    if (activity.compare_exchange_weak(count, count + 1,
                                       cpp::MemoryOrder::ACQ_REL))
      return true;
  }
}

void AioDispatcher::leave() {
  uint64_t previous = activity.fetch_sub(1, cpp::MemoryOrder::RELEASE);
  if (!previous || previous == PARKED)
    __builtin_trap();
}

bool AioDispatcher::try_park() {
  uint64_t idle = 0;
  return activity.compare_exchange_strong(idle, PARKED,
                                          cpp::MemoryOrder::ACQ_REL);
}

void AioDispatcher::resume() {
  uint64_t parked = PARKED;
  if (!activity.compare_exchange_strong(parked, 0, cpp::MemoryOrder::RELEASE))
    __builtin_trap();
  if (channel.control_fd() >= 0)
    (void)channel.wake();
}

void AioDispatcher::delay() {
  const timespec delay{0, 100000000};
  auto result = linux_syscalls::syscall_checked<int>(
      SYS_futex, &ready.value.val, FUTEX_WAIT_PRIVATE, 1, &delay, 0, 0);
  if (!result && result.error() != EINTR && result.error() != EAGAIN &&
      result.error() != ETIMEDOUT)
    dispatch_failure();
}

int AioDispatcher::await_ready() {
  while (!ready.value.load(cpp::MemoryOrder::ACQUIRE)) {
    if (!control->clear_tid.value.load(cpp::MemoryOrder::ACQUIRE))
      dispatch_failure();
    const timespec delay{0, 10000000};
    auto result = linux_syscalls::syscall_checked<int>(
        SYS_futex, &ready.value.val, FUTEX_WAIT_PRIVATE, 0, &delay, 0, 0);
    if (!result && result.error() != EINTR && result.error() != EAGAIN &&
        result.error() != ETIMEDOUT)
      return result.error();
  }
  return int(startup_error.load(cpp::MemoryOrder::ACQUIRE));
}

int AioDispatcher::start(const TLSImage &image, uintptr_t page_size,
                         int descriptor, AioDispatchHandlers input) {
  Operation operation(*this);
  if (!operation)
    return EAGAIN;
  if (!drained())
    return EBUSY;
  if (descriptor < 0 || page_size != 8192 || !input.progress || !input.drained)
    return EINVAL;
#ifndef LIBC_MMIX_PTHREAD_SCHEDULING
  (void)image;
  return ENOTSUP;
#else
  handlers = input;
  ring_fd = descriptor;
  stopping.store(0, cpp::MemoryOrder::RELAXED);
  startup_error.store(0, cpp::MemoryOrder::RELAXED);
  diagnostic.store(0, cpp::MemoryOrder::RELAXED);
  ready.value.store(0, cpp::MemoryOrder::RELAXED);
  auto initialized = channel.initialize();
  if (!initialized)
    return initialized.error();
  ThreadPreparation request;
  request.image = image;
  request.page_size = page_size;
  request.detached = true;
  request.apply_scheduling = true;
  request.scheduling_policy = 0;
  request.scheduling_priority = 0;
  request.runner.posix_runner = entry;
  request.argument = this;
  ThreadAttributes *attributes = nullptr;
  auto created = create_thread_helper(request, attributes, &control);
  if (created.error) {
    retained = created.preparation.retained;
    (void)channel.reset();
    return int(-created.error);
  }
  return await_ready();
#endif
}

int AioDispatcher::wake() {
  Operation operation(*this);
  if (!operation)
    return EAGAIN;
  auto result = channel.wake();
  return result ? 0 : result.error();
}

int AioDispatcher::stop() {
  if (!try_park())
    return EAGAIN;
  int result = stop_parked_impl();
  resume();
  return result;
}

int AioDispatcher::stop_parked() {
  if (activity.load(cpp::MemoryOrder::ACQUIRE) != PARKED)
    __builtin_trap();
  return stop_parked_impl();
}

int AioDispatcher::stop_parked_impl() {
  if (handlers.drained && !handlers.drained(handlers.context))
    return EBUSY;
  stopping.store(1, cpp::MemoryOrder::RELEASE);
  int error = 0;
  if (control) {
    (void)
        channel.wake(); // A timeout also observes stop; do not abandon helper.
    error = int(reclaim_thread(*control, ThreadOwner::Detached, retained));
    control = nullptr;
  } else if (retained.owns_mappings()) {
    error = int(release_thread_resources(retained));
  }
  auto closed = channel.reset();
  if (!error && !closed)
    error = closed.error();
  if (drained()) {
    ring_fd = -1;
    handlers = {};
  }
  return error;
}

void AioDispatcher::quarantine_parked_child() {
  if (activity.load(cpp::MemoryOrder::ACQUIRE) != PARKED)
    __builtin_trap();
  (void)channel.reset();
  ring_fd = -1;
}

void *AioDispatcher::entry(void *argument) {
  auto &dispatcher = *static_cast<AioDispatcher *>(argument);
  uint64_t mask = 0;
  auto queried = linux_syscalls::syscall_checked<int>(
      SYS_rt_sigprocmask, SIG_SETMASK, 0, &mask, sizeof(mask));
  constexpr uint64_t uncatchable =
      (uint64_t(1) << (SIGKILL - 1)) | (uint64_t(1) << (SIGSTOP - 1));
  constexpr uint64_t blocked = ~(uncatchable | CREDENTIAL_MASK);
  int error = queried ? 0 : queried.error();
  if (!error && ((mask & blocked) != blocked || (mask & CREDENTIAL_MASK)))
    error = EPROTO;
  dispatcher.startup_error.store(uint64_t(error), cpp::MemoryOrder::RELEASE);
  dispatcher.ready.value.store(1, cpp::MemoryOrder::RELEASE);
  if (!dispatcher.ready.value.notify_all())
    dispatch_failure();
  if (!error)
    dispatcher.run();
  while (!dispatcher.stopping.load(cpp::MemoryOrder::ACQUIRE))
    dispatcher.delay();
  return nullptr;
}

void AioDispatcher::run() {
  while (!stopping.load(cpp::MemoryOrder::ACQUIRE)) {
    bool parked = false;
    {
      Operation operation(*this);
      parked = !operation;
      if (operation && !stopping.load(cpp::MemoryOrder::ACQUIRE)) {
        int error = handlers.progress(handlers.context);
        if (error < 0)
          dispatch_failure();
        if (error)
          diagnostic.store(uint64_t(error), cpp::MemoryOrder::RELEASE);
      }
    }
    if (parked) {
      delay();
      continue;
    }
    const timespec interval{0, 100000000};
    auto result = channel.wait(ring_fd, &interval);
    if (!result && result.error() != EINTR) {
      if (result.error() != EAGAIN && result.error() != ENOMEM)
        dispatch_failure();
      // Poll resource pressure must not create an unbounded busy retry loop.
      delay();
    }
  }
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
