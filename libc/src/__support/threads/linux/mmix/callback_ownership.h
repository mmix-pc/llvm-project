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
#include "src/__support/OSUtil/linux/mmix/dynamic_tls.h"

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

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
