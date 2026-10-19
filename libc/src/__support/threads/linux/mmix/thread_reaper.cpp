//===-- MMIX Linux reaper bootstrap ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "thread_reaper.h"
#include "thread_reclaim.h"
#include "src/__support/threads/thread.h"

#if LIBC_THREAD_MODE != LIBC_THREAD_MODE_PLATFORM || \
    LIBC_ERRNO_MODE != LIBC_ERRNO_MODE_THREAD_LOCAL
#error "MMIX Linux reaper bootstrap requires platform TLS and TLS errno"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// Independent of the registry: initialization may allocate, clone and wait.
// The child/service never takes this lock, including failed-start reclamation.
struct ReaperBootstrap {
  alignas(8) RawMutex mutex;
  ThreadAttributes *attributes = nullptr;
  ThreadControl *control = nullptr;
  cpp::Atomic<bool> stopping{false};
};
static LIBC_CONSTINIT ReaperBootstrap bootstrap;

void restore_reaper_after_fork() {
  RawMutex::init(&bootstrap.mutex);
  bootstrap.attributes = nullptr;
  bootstrap.control = nullptr;
  bootstrap.stopping.store(false);
}

bool reaper_stop_requested() {
  return bootstrap.stopping.load(cpp::MemoryOrder::ACQUIRE);
}

bool retire_reaper_for_fork(ThreadControl &caller) {
#ifdef LIBC_MMIX_SHARED_STARTUP
  // Retirement releases loader owners. A caller already inside a loader
  // transaction must not wait for the helper to acquire that same lock.
  if (caller.loader_scopes.load() || caller.loader_callbacks.load())
    return false;
#endif
  // Never wait for a creator: it may need a lock owned by an atfork callback.
  if (!bootstrap.mutex.try_lock())
    return false;
  bool eligible;
  {
    ThreadRegistryLock lock(thread_registry);
    eligible = lock.sole_fork_caller(caller);
  }
  if (eligible && bootstrap.attributes) {
    // Bootstrap owns the helper's exclusive lifecycle pin, independently of
    // the application handle registry and the ThreadControl member layout.
    bootstrap.stopping.store(true, cpp::MemoryOrder::RELEASE);
    ThreadResources retained;
    if (reclaim_thread(*bootstrap.control, ThreadOwner::Detached, retained))
      __builtin_trap();
    bootstrap.attributes = nullptr;
    bootstrap.control = nullptr;
    bootstrap.stopping.store(false, cpp::MemoryOrder::RELEASE);
  }
  bootstrap.mutex.unlock();
  return eligible;
}

static void *reaper_runner(void *) {
  if (!internal::self.attrib)
    __builtin_trap();
  __llvm_libc_mmix_reaper_loop();
  if (!reaper_stop_requested())
    __builtin_trap();
  return nullptr;
}

ThreadCreationResult ensure_thread_reaper(const TLSImage &image,
                                         uintptr_t page_size) {
  bootstrap.mutex.lock();
  ThreadCreationResult result;
  if (!bootstrap.attributes) {
    ThreadPreparation request;
    request.image = image;
    request.page_size = page_size;
    request.detached = true;
    request.runner.posix_runner = reaper_runner;
    ThreadAttributes *created = nullptr;
    result = create_thread_helper(request, created, &bootstrap.control);
    if (!result.error)
      bootstrap.attributes = created;
    // A failed rollback must not lose descriptors through a later retry.
    if (result.preparation.rollback_error)
      __builtin_trap();
  }
  if (bootstrap.attributes) {
    auto *clear = static_cast<Futex *>(bootstrap.attributes->platform_data);
    if (!clear || clear->load(cpp::MemoryOrder::ACQUIRE) == 0)
      __builtin_trap();
  }
  bootstrap.mutex.unlock();
  return result;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
