//===-- MMIX Linux image-owned process callbacks --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/atexit.h"
#include "src/__support/CPP/new.h"
#include "src/__support/OSUtil/linux/mmix/dynamic_tls.h"
#include "src/__support/threads/linux/mmix/loader_cancellation.h"
#include "src/stdlib/exit_handler.h"
#include "src/stdlib/free.h"
#include "src/stdlib/malloc.h"

namespace LIBC_NAMESPACE_DECL {

// Also used by at_quick_exit; never hold it across user code.
Mutex handler_list_mtx(false, false, false, false);

namespace {
class CancellationScope {
  loader::LoaderCancellation hooks = mmix::loader_cancellation_hooks();
  uint64_t saved = 0;

public:
  CancellationScope() {
    if (hooks.enter(&saved))
      __builtin_trap();
  }
  ~CancellationScope() noexcept(false) {
    if (hooks.leave(saved))
      __builtin_trap();
  }
};

struct Callback {
  AtExitCallback *function;
  void *object;
  void *dso;
  Callback *next;
  uint64_t pin;
};
Callback *head;

int register_callback(AtExitCallback *function, void *object, void *dso,
                      uintptr_t code) {
  if (!function)
    return -1;
  CancellationScope scope;
  uint64_t pin = 0;
  // Without a DSO handle, keep the actual C callback's image as a runtime
  // owner until process exit, not merely the libc trampoline's image.
  long error = dso ? mmix::retain_shared_process_callback(dso, code, &pin)
                   : mmix::retain_shared_tls_destructor(nullptr, code, &pin);
  if (error)
    return -1;
  void *storage = LIBC_NAMESPACE::malloc(sizeof(Callback));
  if (!storage) {
    if (mmix::release_shared_tls_destructor(pin))
      __builtin_trap();
    return -1;
  }
  auto *node = new (storage) Callback{function, object, dso, nullptr, pin};
  cpp::lock_guard lock(handler_list_mtx);
  node->next = head;
  head = node;
  return 0;
}

// Termination callbacks cannot escape through the loader's no-EH frames.
void invoke(Callback &node) noexcept { node.function(node.object); }
} // namespace

extern "C" int __cxa_atexit(AtExitCallback *function, void *object, void *dso) {
  return register_callback(function, object, dso,
                           reinterpret_cast<uintptr_t>(function));
}

extern "C" void __cxa_finalize(void *dso) {
  CancellationScope scope;
  for (;;) {
    Callback *node;
    {
      cpp::lock_guard lock(handler_list_mtx);
      Callback **slot = &head;
      while (*slot && dso && (*slot)->dso != dso)
        slot = &(*slot)->next;
      node = *slot;
      if (!node)
        return;
      *slot = node->next;
    }
    // Detach before invocation, so recursive finalization cannot replay it.
    // Retain ownership across invocation, including recursive registrations.
    invoke(*node);
    if (mmix::release_shared_tls_destructor(node->pin))
      __builtin_trap();
    LIBC_NAMESPACE::free(node);
  }
}

LLVM_LIBC_FUNCTION(int, atexit, (__atexithandler_t callback)) {
  return callback
             ? register_callback(stdc_at_exit_func,
                                 reinterpret_cast<void *>(callback), nullptr,
                                 reinterpret_cast<uintptr_t>(callback))
             : -1;
}

} // namespace LIBC_NAMESPACE_DECL
