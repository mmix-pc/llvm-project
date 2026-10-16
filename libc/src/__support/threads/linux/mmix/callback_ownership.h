//===-- MMIX shared callback ownership --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CALLBACK_OWNERSHIP_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CALLBACK_OWNERSHIP_H

#include "loader_cancellation.h"
#include "src/__support/CPP/new.h"
#include "src/__support/OSUtil/linux/mmix/dynamic_tls.h"
#include "src/stdlib/free.h"
#include "src/stdlib/malloc.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
class CallbackOwnershipScope {
  loader::LoaderCancellation hooks = loader_cancellation_hooks();
  uint64_t saved = 0;

public:
  CallbackOwnershipScope() {
    if (hooks.enter(&saved))
      __builtin_trap();
  }
  ~CallbackOwnershipScope() noexcept(false) {
    if (hooks.leave(saved))
      __builtin_trap();
  }
};

// Owner-thread sidecar stack preserves the public cleanup-record ABI.
// Callers suppress cancellation through publication or detached invocation.
class CallbackOwners {
public:
  struct Node {
    const void *record;
    uint64_t pin;
    Node *next;
  };

private:
  Node *head = nullptr;

public:
  bool push(const void *record, uintptr_t callback) {
    uint64_t pin = 0;
    if (retain_shared_tls_destructor(nullptr, callback, &pin))
      return false;
    void *memory = LIBC_NAMESPACE::malloc(sizeof(Node));
    if (!memory) {
      if (release_shared_tls_destructor(pin))
        __builtin_trap();
      return false;
    }
    head = new (memory) Node{record, pin, head};
    return true;
  }
  Node *detach(const void *record) {
    if (!head || head->record != record)
      __builtin_trap();
    auto *node = head;
    head = node->next;
    return node;
  }
  static void release(Node *node) {
    if (release_shared_tls_destructor(node->pin))
      __builtin_trap();
    LIBC_NAMESPACE::free(node);
  }
};
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
