//===-- Runtime TLS resolution ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_RUNTIME_TLS_H
#define LLVM_LIBC_LOADER_LINUX_RUNTIME_TLS_H

#include "loader/linux/dtv.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

struct RuntimeTLSContext {
  // Caller suppresses cancellation before acquiring/pinning a published scope
  // snapshot. Allocation and refresh state belong to the executing thread.
  const ImageRegistry::Snapshot *registry;
  ThreadTLSAllocation *allocation;
  DTVRefresh *refresh;
  Address page_size;
};

// Startup entries are reused; late GD/LD entries are allocated lazily. This
// service is not async-signal-safe and must not be called by signal handlers.
// Invalid descriptors, allocation and publication failures terminate; never
// return a null TLS address. Context/pinning and thread retirement are owned
// by the service adapter, not this component.
void *resolve_runtime_tls(void *context, const TLSIndex *index);

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
