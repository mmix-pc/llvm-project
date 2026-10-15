//===-- MMIX Linux image-owned process callbacks --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/atexit.h"
#include "src/stdlib/exit_handler.h"
#include "src/stdlib/free.h"
#include "src/stdlib/malloc.h"
#include "src/__support/CPP/new.h"

namespace LIBC_NAMESPACE_DECL {

// Also used by at_quick_exit; never hold it across user code.
Mutex handler_list_mtx(false, false, false, false);

namespace {
struct Callback {
  AtExitCallback *function;
  void *object;
  void *dso;
  Callback *next;
};
Callback *head;
}

extern "C" int __cxa_atexit(AtExitCallback *function, void *object, void *dso) {
  if (!function)
    return -1;
  void *storage = LIBC_NAMESPACE::malloc(sizeof(Callback));
  if (!storage)
    return -1;
  auto *node = new (storage) Callback{function, object, dso, nullptr};
  cpp::lock_guard lock(handler_list_mtx);
  node->next = head;
  head = node;
  return 0;
}

extern "C" void __cxa_finalize(void *dso) {
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
    // Resident images retain the DSO and callback code through completion.
    node->function(node->object);
    LIBC_NAMESPACE::free(node);
  }
}

LLVM_LIBC_FUNCTION(int, atexit, (__atexithandler_t callback)) {
  return callback ? __cxa_atexit(stdc_at_exit_func,
                               reinterpret_cast<void *>(callback), nullptr)
                  : -1;
}

} // namespace LIBC_NAMESPACE_DECL
