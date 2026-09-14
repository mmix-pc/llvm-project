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
#include "src/__support/threads/thread.h"
#include <sys/syscall.h>

#if LIBC_THREAD_MODE != LIBC_THREAD_MODE_SINGLE
#error "MMIX Linux main-thread state requires single-thread mode"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {

// FIXME: Replace process-global state with per-thread storage and loader-aware
// lifecycle integration when Linux pthread/TLS support is available.
ThreadAttributes main_thread_attributes;
enum class Phase { Uninitialized, Active, Cleaning, Finished };
Phase phase = Phase::Uninitialized;

} // namespace

bool initialize_main_thread() {
  if (phase != Phase::Uninitialized)
    return phase == Phase::Active;

  long tid = syscall_impl(SYS_gettid);
  if (tid <= 0 || tid > cpp::numeric_limits<int>::max())
    return false;

  main_thread_attributes.tid = static_cast<int>(tid);
  main_thread_attributes.atexit_callback_mgr = get_thread_atexit_callback_mgr();
  self.attrib = &main_thread_attributes;
  phase = Phase::Active;
  return true;
}

void cleanup_main_thread() {
  if (phase != Phase::Active)
    return;
  phase = Phase::Cleaning;
  call_atexit_callbacks(&main_thread_attributes);
  phase = Phase::Finished;
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
