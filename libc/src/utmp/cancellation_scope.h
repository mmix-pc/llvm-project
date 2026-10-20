//===-- Accounting cancellation policy --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_UTMP_CANCELLATION_SCOPE_H
#define LLVM_LIBC_SRC_UTMP_CANCELLATION_SCOPE_H

#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace utmp_backend {

// CMake selects the cancellation policy; the generic implementation is a no-op.
// Construct before resource guards so any restoration happens after cleanup.
class CancellationScope {
  [[maybe_unused]] int saved = 0;
  [[maybe_unused]] bool restore = false;

public:
  CancellationScope();
  ~CancellationScope() noexcept(false);
  CancellationScope(const CancellationScope &) = delete;
  CancellationScope &operator=(const CancellationScope &) = delete;
};

} // namespace utmp_backend
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_UTMP_CANCELLATION_SCOPE_H
