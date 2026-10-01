//===-- MMIX Linux TLS destructor registration and draining
//----------------===//
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

namespace LIBC_NAMESPACE_DECL {

bool ThreadAtExitCallbackMgr::is_owner() const {
  auto *control = mmix::current_control;
  return control && internal::self.attrib == &control->attributes &&
         control->attributes.atexit_callback_mgr == this;
}

int ThreadAtExitCallbackMgr::add_callback(Callback *callback, void *object) {
  if (!is_owner() || !callback || phase == Phase::Done)
    return -1;
  void *memory = LIBC_NAMESPACE::malloc(sizeof(Node));
  if (!memory)
    return -1;
  // Publish only after allocation succeeds; retain every older registration.
  head = new (memory) Node{callback, object, head};
  return 0;
}

void ThreadAtExitCallbackMgr::call() {
  if (!is_owner() || phase == Phase::Draining)
    __builtin_trap();
  if (phase == Phase::Done)
    return;
  phase = Phase::Draining;
  while (head) {
    Node *node = head;
    Callback *callback = node->callback;
    void *object = node->object;
    head = node->next;
    LIBC_NAMESPACE::free(node);
    // No runtime lock or registration allocation remains live across a
    // callback. New registrations become the next entries in this same drain.
    callback(object);
  }
  phase = Phase::Done;
}

} // namespace LIBC_NAMESPACE_DECL
