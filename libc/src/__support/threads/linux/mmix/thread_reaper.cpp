//===-- MMIX Linux reaper bootstrap ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "thread_reaper.h"
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
};
static LIBC_CONSTINIT ReaperBootstrap bootstrap;

static void *reaper_runner(void *) {
  if (!internal::self.attrib)
    __builtin_trap();
  __llvm_libc_mmix_reaper_loop();
  __builtin_trap();
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
    result = create_thread_helper(request, created);
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
