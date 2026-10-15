//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "cxxabi.h"
#include <stdint.h>

extern "C" {
_LIBCXXABI_FUNC_VIS void __cxa_mmix_invoke_initializer(uintptr_t, int, char **,
                                                     char **) noexcept;
_LIBCXXABI_FUNC_VIS void __cxa_mmix_invoke_finalizer(uintptr_t) noexcept;
}

// Loader callbacks may catch internally, but must not unwind across the
// loader's mutation/cancellation scope or turn committed state into rollback.
extern "C" _LIBCXXABI_FUNC_VIS void
__cxa_mmix_invoke_initializer(uintptr_t address, int argc, char **argv,
                             char **envp) noexcept {
  reinterpret_cast<void (*)(int, char **, char **)>(address)(argc, argv, envp);
}

extern "C" _LIBCXXABI_FUNC_VIS void
__cxa_mmix_invoke_finalizer(uintptr_t address) noexcept {
  reinterpret_cast<void (*)()>(address)();
}
