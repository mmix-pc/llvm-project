//===-- Startup TLS resolver ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_TLS_RESOLVER_H
#define LLVM_LIBC_LOADER_LINUX_TLS_RESOLVER_H
#include "loader/linux/tls_allocation.h"
namespace LIBC_NAMESPACE_DECL {
namespace loader {
// Trusted immutable context and readable runtime-owned TP/DTV storage are
// required. This is not recovery from arbitrary application memory corruption.
void *resolve_startup_tls(void *context, const TLSIndex *index);
// Target hook; reads the calling thread's TP without modifying it.
Address current_thread_pointer();
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
