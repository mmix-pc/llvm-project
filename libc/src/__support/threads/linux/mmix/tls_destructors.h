//===-- MMIX Linux TLS destructor storage ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_TLS_DESTRUCTORS_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_TLS_DESTRUCTORS_H

#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

// Selected by the MMIX static threaded runtime. Only the owning thread may
// register or drain; callbacks run before TSS and while TLS is still installed.
class ThreadAtExitCallbackMgr {
  using Callback = void(void *);
  struct Node {
    Callback *callback;
    void *object;
    Node *next;
  };
  enum class Phase { Open, Draining, Done };
  Node *head = nullptr;
  Phase phase = Phase::Open;

  bool is_owner() const;

public:
  constexpr ThreadAtExitCallbackMgr() = default;
  ThreadAtExitCallbackMgr(const ThreadAtExitCallbackMgr &) = delete;
  ThreadAtExitCallbackMgr &operator=(const ThreadAtExitCallbackMgr &) = delete;

  int add_callback(Callback *callback, void *object);
  void call();
};

} // namespace LIBC_NAMESPACE_DECL
#endif
