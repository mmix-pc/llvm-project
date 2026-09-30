//===-- MMIX Linux main-thread lifecycle ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "main_thread.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/thread.h"
#include <sys/syscall.h>

#if LIBC_THREAD_MODE != LIBC_THREAD_MODE_SINGLE &&                             \
    (LIBC_THREAD_MODE != LIBC_THREAD_MODE_PLATFORM ||                          \
     LIBC_ERRNO_MODE != LIBC_ERRNO_MODE_THREAD_LOCAL)
#error "MMIX Linux TLS main-thread state requires platform TLS and TLS errno"
#endif

namespace LIBC_NAMESPACE_DECL {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
namespace mmix {
LIBC_CONSTINIT LIBC_THREAD_LOCAL ThreadControl *current_control = nullptr;
}
#endif
namespace internal {
namespace {

#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_SINGLE
ThreadAttributes main_thread_attributes;
#endif
ThreadAttributes *active_attributes = nullptr;
enum class Phase { Uninitialized, Active, Cleaning, Finished };
Phase phase = Phase::Uninitialized;

} // namespace

bool initialize_main_thread() {
  if (phase != Phase::Uninitialized)
    return phase == Phase::Active;

#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_SINGLE
  long tid = syscall_impl(SYS_gettid);
  if (tid <= 0 || tid > cpp::numeric_limits<int>::max())
    return false;

  main_thread_attributes.tid = static_cast<int>(tid);
  main_thread_attributes.atexit_callback_mgr = get_thread_atexit_callback_mgr();
  self.attrib = &main_thread_attributes;
  active_attributes = &main_thread_attributes;
  phase = Phase::Active;
  return true;
#else
  // TLS startup must activate prepared state before lifecycle callbacks.
  return false;
#endif
}

#if LIBC_THREAD_MODE != LIBC_THREAD_MODE_SINGLE
bool activate_main_thread(MainThreadState &state) {
  if (phase != Phase::Uninitialized || self.attrib || state.attributes.tid <= 0)
    return false;
  self.attrib = &state.attributes;
  libc_errno = 0;
  auto *manager = get_thread_atexit_callback_mgr();
  if (!manager ||
      syscall_impl(SYS_set_tid_address,
                   reinterpret_cast<long>(&state.clear_tid.value.val)) !=
          state.attributes.tid) {
    self.attrib = nullptr;
    return false;
  }
  state.attributes.atexit_callback_mgr = manager;
  {
    // Startup has not installed user handlers or published any worker. Do not
    // run callback setup or the clear-TID syscall while holding this lock.
    mmix::ThreadRegistryLock lock(mmix::thread_registry);
    if (!lock.register_main(state)) {
      self.attrib = nullptr;
      return false;
    }
  }
  active_attributes = &state.attributes;
  mmix::current_control = &state;
  phase = Phase::Active;
  return true;
}
#endif

void cleanup_main_thread() {
  if (phase != Phase::Active)
    return;
  phase = Phase::Cleaning;
  call_atexit_callbacks(active_attributes);
  phase = Phase::Finished;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
