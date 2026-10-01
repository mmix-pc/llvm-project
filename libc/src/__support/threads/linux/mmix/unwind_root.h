//===-- MMIX Linux unwind roots --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_UNWIND_ROOT_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_UNWIND_ROOT_H

#include "src/__support/macros/config.h"
#include <stdint.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
struct ThreadControl;

// Private runtime state, not a public pthread or kernel ABI.
struct UnwindRoot {
  ThreadControl *owner = nullptr;
  uintptr_t ro = 0, procedure = 0, sp = 0;
  uint64_t chain = 0, generation = 0;
  bool active = false;
};

using RootBody = void(void *);
void dispatch_c_cleanup(uintptr_t ro, uintptr_t procedure, uintptr_t sp);
bool has_c_cleanup();
[[gnu::visibility("hidden")]] void
run_with_unwind_root(ThreadControl &control, RootBody *body, void *argument);

// Shared root bridge. Only the selected forced-exit composition connects public
// pthread_exit here; retained C-only packages keep their direct finish path.
extern "C" [[noreturn, gnu::visibility("hidden")]] void
__llvm_libc_mmix_unwind_to_root(void *result);
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
