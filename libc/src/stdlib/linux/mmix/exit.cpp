//===-- MMIX Linux normal termination -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/exit.h"
#include "src/__support/OSUtil/exit.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/main_thread.h"

namespace LIBC_NAMESPACE_DECL {

extern "C" void __cxa_finalize(void *);

[[noreturn]] LLVM_LIBC_FUNCTION(void, exit, (int status)) {
  // Explicit exit, including from constructors, shares main's cleanup path.
  internal::cleanup_main_thread();
  __cxa_finalize(nullptr);
  internal::exit(status);
}

} // namespace LIBC_NAMESPACE_DECL
