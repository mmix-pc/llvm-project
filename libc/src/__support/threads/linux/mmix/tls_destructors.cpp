//===-- MMIX Linux TLS destructor registration and draining ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "tls_destructors.h"
#include "lifecycle.h"
#include "src/__support/CPP/new.h"
#include "src/__support/threads/thread.h"
#include "src/stdlib/free.h"
#include "src/stdlib/malloc.h"
#ifdef LIBC_MMIX_SHARED_STARTUP
#include "loader_cancellation.h"
#include "src/__support/OSUtil/linux/mmix/dynamic_tls.h"
#endif

namespace LIBC_NAMESPACE_DECL {

#ifdef LIBC_MMIX_SHARED_STARTUP
namespace {
// Retain/publication and callback/release are indivisible with cancellation.
class RegistrationScope {
  loader::LoaderCancellation hooks = mmix::loader_cancellation_hooks();
  uint64_t saved = 0;

public:
  RegistrationScope() {
    if (hooks.enter(&saved))
      __builtin_trap();
  }
  ~RegistrationScope() noexcept(false) {
    if (hooks.leave(saved))
      __builtin_trap();
  }
};
} // namespace
#endif

bool ThreadAtExitCallbackMgr::is_owner() const {
  auto *control = mmix::current_control;
  return control && internal::self.attrib == &control->attributes &&
         control->attributes.atexit_callback_mgr == this;
}

int ThreadAtExitCallbackMgr::add_callback(Callback *callback, void *object,
                                        void *dso) {
  if (!is_owner() || !callback || phase == Phase::Done)
    return -1;
#ifdef LIBC_MMIX_SHARED_STARTUP
  RegistrationScope scope;
  uint64_t pin = 0;
  if (mmix::retain_shared_tls_destructor(
          dso, reinterpret_cast<uintptr_t>(callback), &pin))
    return -1;
#endif
  void *memory = LIBC_NAMESPACE::malloc(sizeof(Node));
  if (!memory) {
#ifdef LIBC_MMIX_SHARED_STARTUP
    if (mmix::release_shared_tls_destructor(pin))
      __builtin_trap();
#endif
    return -1;
  }
  // Publish only after allocation succeeds; retain every older registration.
  auto *node = new (memory) Node{callback, object, dso, head};
#ifdef LIBC_MMIX_SHARED_STARTUP
  node->pin = pin;
#endif
  head = node;
  return 0;
}

void ThreadAtExitCallbackMgr::call() {
  if (!is_owner() || phase == Phase::Draining)
    __builtin_trap();
  if (phase == Phase::Done)
    return;
#ifdef LIBC_MMIX_SHARED_STARTUP
  RegistrationScope scope;
#endif
  phase = Phase::Draining;
  while (head) {
    Node *node = head;
    Callback *callback = node->callback;
    void *object = node->object;
    head = node->next;
    // Keep the ownership record through callback completion, outside the list
    // and without a runtime lock. Nested registrations drain next.
    callback(object);
#ifdef LIBC_MMIX_SHARED_STARTUP
    if (mmix::release_shared_tls_destructor(node->pin))
      __builtin_trap();
#endif
    LIBC_NAMESPACE::free(node);
  }
  phase = Phase::Done;
}

} // namespace LIBC_NAMESPACE_DECL
