//===-- MMIX AIO completion dispatch lifecycle ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_AIO_DISPATCHER_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_AIO_DISPATCHER_H

#include "src/__support/OSUtil/linux/aio_dispatch_channel.h"
#include "thread_create.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

struct AioDispatchHandlers {
  void *context = nullptr;
  // Resident runtime code only. Positive diagnostics describe a completed
  // maintenance pass, not abandonment of pending I/O. Integrity/ownership
  // failures must terminate rather than manufacturing original completions.
  int (*progress)(void *) = nullptr;
  // Called exclusively while parked. Includes requests, rollback, controls,
  // notifications and callback/reclamation owners, not just an empty CQ.
  bool (*drained)(void *) = nullptr;
};

// One service-owned helper. External callers serialize start/stop/checkpoint,
// pin the service, suppress cancellation and block application signals while
// leaving credential notification enabled. Operation covers the ENTIRE public
// transaction, including preparation outside the registry gate and final wake.
// This is not lazy process publication or a public AIO provider.
class AioDispatcher {
  aio_dispatch_channel::Channel<> channel;
  AioDispatchHandlers handlers{};
  int ring_fd = -1; // Borrowed; owned by the service until successful stop.
  ThreadControl *control = nullptr;
  ThreadResources retained{};
  LifecycleWord ready;
  cpp::Atomic<uint64_t> stopping{0};
  cpp::Atomic<uint64_t> startup_error{0};
  cpp::Atomic<uint64_t> diagnostic{0};
  cpp::Atomic<uint64_t> activity{0};
  static constexpr uint64_t PARKED = UINT64_MAX;

  bool enter();
  void leave();
  static void *entry(void *);
  void run();
  void delay();
  int await_ready();
  int stop_parked_impl();

public:
  class Operation {
    AioDispatcher *owner;

  public:
    explicit Operation(AioDispatcher &dispatcher)
        : owner(dispatcher.enter() ? &dispatcher : nullptr) {}
    ~Operation() {
      if (owner)
        owner->leave();
    }
    Operation(const Operation &) = delete;
    Operation &operator=(const Operation &) = delete;
    explicit operator bool() const { return owner != nullptr; }
  };

  AioDispatcher() = default;
  AioDispatcher(const AioDispatcher &) = delete;
  AioDispatcher &operator=(const AioDispatcher &) = delete;
  ~AioDispatcher() {
    if (!drained() || activity.load(cpp::MemoryOrder::RELAXED))
      __builtin_trap();
  }

  // No requests may be admitted until start succeeds. Failures can retain
  // helper or failed-release ownership; call stop() before retry/destruction.
  int start(const TLSImage &, uintptr_t page_size, int descriptor,
            AioDispatchHandlers);
  // Signal a previously accepted transaction. A wake failure does not revoke
  // acceptance: the bounded retry interval still drives progress/cleanup.
  int wake();
  int last_diagnostic() {
    return int(diagnostic.load(cpp::MemoryOrder::ACQUIRE));
  }
  bool try_park();
  void resume();
  // EAGAIN while transactions run, EBUSY while service owners remain. Neither
  // result frees resources or stops pending requests. Successful stop permits
  // ring destruction. Reclamation failure retains mappings for a later retry.
  int stop();
  int stop_parked();
  // Copied child only, with checkpoint held. Close only the copied wake fd;
  // never wake/reap vanished helpers or consume the parent's ring. The whole
  // copied dispatcher must remain inaccessible and must not be destroyed.
  void quarantine_parked_child();
  bool drained() const {
    return !control && !retained.owns_mappings() && channel.control_fd() < 0;
  }
};

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
