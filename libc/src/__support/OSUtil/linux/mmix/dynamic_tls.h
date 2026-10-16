//===-- Startup TLS resolver ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_MMIX_DYNAMIC_TLS_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_MMIX_DYNAMIC_TLS_H
#include "src/__support/OSUtil/linux/loader_services.h"
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
// Called once before publishing threads or entering TLS-dependent libc.
// The immutable context and service remain valid for the process lifetime.
__attribute__((visibility("hidden"), no_instrument_function)) bool
adopt_startup_tls_resolver(const loader::StartupTLSResolver &service);
// Shared startup publishes these process-resident services before any clone.
// Failed allocation/release retains all still-owned mappings in allocation.
long prepare_shared_thread_tls(void *state,
                               loader::ThreadTLSAllocation &allocation);
long release_shared_thread_tls(loader::ThreadTLSAllocation &allocation);
long retain_shared_tls_destructor(void *dso, uintptr_t callback,
                                  uint64_t *token);
long release_shared_tls_destructor(uint64_t token);
long retain_shared_process_callback(void *dso, uintptr_t callback,
                                    uint64_t *token);
[[noreturn]] __attribute__((visibility("hidden"), no_instrument_function))
void fatal_dynamic_tls();
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

extern "C" __attribute__((visibility("hidden"), no_instrument_function)) void *
__llvm_libc_tls_get_addr(const LIBC_NAMESPACE::loader::TLSIndex *);
extern "C" __attribute__((visibility("default"))) void *
__tls_get_addr(const LIBC_NAMESPACE::loader::TLSIndex *);
#endif
