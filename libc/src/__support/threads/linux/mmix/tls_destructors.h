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
#ifdef LIBC_MMIX_SHARED_STARTUP
#include "hdr/stdint_proxy.h"
#endif

namespace LIBC_NAMESPACE_DECL {

// Selected by the MMIX threaded runtime. Only the owning thread may
// register or drain; callbacks run before TSS and while TLS is still installed.
class ThreadAtExitCallbackMgr {
  using Callback = void(void *);
  struct Node {
    Callback *callback;
    void *object;
    // The CRT handle identifies the owning image, not the TLS object mapping.
    void *dso;
    Node *next;
#ifdef LIBC_MMIX_SHARED_STARTUP
    uint64_t pin = 0;
#endif
  };
  enum class Phase { Open, Draining, Done };
  Node *head = nullptr;
  Phase phase = Phase::Open;

  bool is_owner() const;

public:
  constexpr ThreadAtExitCallbackMgr() = default;
  ThreadAtExitCallbackMgr(const ThreadAtExitCallbackMgr &) = delete;
  ThreadAtExitCallbackMgr &operator=(const ThreadAtExitCallbackMgr &) = delete;

  int add_callback(Callback *callback, void *object, void *dso = nullptr);
  void call();
};

} // namespace LIBC_NAMESPACE_DECL
#endif
