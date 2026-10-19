//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/quick_exit.h"
#include "process_exit.h"
#include "src/__support/CPP/mutex.h"
#include "src/__support/CPP/new.h"
#include "src/__support/OSUtil/exit.h"
#include "src/__support/threads/mutex.h"
#include "src/stdlib/at_quick_exit.h"
#include "src/stdlib/free.h"
#include "src/stdlib/malloc.h"
#ifdef LIBC_MMIX_QUICK_EXIT_CANCELLATION
#include "src/__support/threads/linux/mmix/loader_cancellation.h"
#endif
#ifdef LIBC_MMIX_SHARED_STARTUP
#include "src/__support/OSUtil/linux/mmix/dynamic_tls.h"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
// Shared with the coordinated fork snapshot, never held across user code.
Mutex quick_exit_mutex(false, false, false, false);
}
namespace {
class RegistrationScope {
#ifdef LIBC_MMIX_QUICK_EXIT_CANCELLATION
  loader::LoaderCancellation hooks = mmix::loader_cancellation_hooks();
  uint64_t saved = 0;
#endif
public:
  RegistrationScope() {
#ifdef LIBC_MMIX_QUICK_EXIT_CANCELLATION
    if (hooks.enter(&saved))
      __builtin_trap();
#endif
  }
  ~RegistrationScope() noexcept(false) {
#ifdef LIBC_MMIX_QUICK_EXIT_CANCELLATION
    if (hooks.leave(saved))
      __builtin_trap();
#endif
  }
};
struct QuickCallback {
  __atexithandler_t function;
  QuickCallback *next;
#ifdef LIBC_MMIX_SHARED_STARTUP
  uint64_t pin = 0;
#endif
};
QuickCallback *quick_head;
bool quick_done;

// A callback must not unwind through process termination.
void invoke(__atexithandler_t function) noexcept { function(); }
} // namespace

LLVM_LIBC_FUNCTION(int, at_quick_exit, (__atexithandler_t function)) {
  if (!function)
    return -1;
  RegistrationScope scope;
#ifdef LIBC_MMIX_SHARED_STARTUP
  uint64_t pin = 0;
  // Quick callbacks are process-lifetime owners, not DSO finalizers.
  if (mmix::retain_shared_tls_destructor(
          nullptr, reinterpret_cast<uintptr_t>(function), &pin))
    return -1;
#endif
  void *memory = LIBC_NAMESPACE::malloc(sizeof(QuickCallback));
  if (!memory) {
#ifdef LIBC_MMIX_SHARED_STARTUP
    if (mmix::release_shared_tls_destructor(pin))
      __builtin_trap();
#endif
    return -1;
  }
  auto *node = new (memory) QuickCallback{function, nullptr};
#ifdef LIBC_MMIX_SHARED_STARTUP
  node->pin = pin;
#endif
  {
    cpp::lock_guard lock(mmix::quick_exit_mutex);
    if (!quick_done) {
      node->next = quick_head;
      quick_head = node;
      return 0;
    }
  }
#ifdef LIBC_MMIX_SHARED_STARTUP
  if (mmix::release_shared_tls_destructor(pin))
    __builtin_trap();
#endif
  LIBC_NAMESPACE::free(node);
  return -1;
}

[[noreturn]] LLVM_LIBC_FUNCTION(void, quick_exit, (int status)) {
  // Suppress cancellation through ownership election, callbacks and exit_group.
  // No scope restoration runs on this non-returning path.
  RegistrationScope scope;
  internal::begin_process_exit(status);
  for (;;) {
    QuickCallback *node;
    {
      cpp::lock_guard lock(mmix::quick_exit_mutex);
      node = quick_head;
      if (!node) {
        quick_done = true;
        break;
      }
      quick_head = node->next;
    }
    invoke(node->function);
#ifdef LIBC_MMIX_SHARED_STARTUP
    if (mmix::release_shared_tls_destructor(node->pin))
      __builtin_trap();
#endif
    LIBC_NAMESPACE::free(node);
  }
  // No ordinary callbacks, TLS/TSS destructors or stdio flushing.
  internal::exit(status);
}
} // namespace LIBC_NAMESPACE_DECL
