//===-- MMIX Linux shared provider exports -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_CONFIG_LINUX_MMIX_SHARED_EXPORTS_H
#define LLVM_LIBC_CONFIG_LINUX_MMIX_SHARED_EXPORTS_H

#include "src/__support/macros/config.h"

// These ABI entrypoints do not use LLVM_LIBC_FUNCTION. Keep their publication
// in the shared target configuration, without exporting the internal namespace.
namespace LIBC_NAMESPACE_DECL {
extern "C" {
[[gnu::visibility("default")]] int *__llvm_libc_errno() noexcept;
[[gnu::visibility("default")]] int __cxa_atexit(void (*)(void *), void *, void *);
[[gnu::visibility("default")]] void __cxa_finalize(void *);
[[gnu::visibility("default")]] int
__cxa_thread_atexit_impl(void (*)(void *), void *, void *);
}
} // namespace LIBC_NAMESPACE_DECL
#endif
