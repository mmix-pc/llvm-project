//===-- MMIX Linux active once initializers --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_ONCE_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_ONCE_H

#include "src/__support/threads/linux/callonce.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

class OnceInitializer {
  CallOnceFlag *flag;
  OnceInitializer *previous;
  friend void abandon_once_initializers();

public:
  explicit OnceInitializer(CallOnceFlag *flag);
  ~OnceInitializer();
  OnceInitializer(const OnceInitializer &) = delete;
  OnceInitializer &operator=(const OnceInitializer &) = delete;
};

// Retained direct C exit drains records before reclamation. In the forced-exit
// profile, stack unwinding already removed them and the common guard reset them.
// Cancellation points remain a separate runtime integration.
void abandon_once_initializers();

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
