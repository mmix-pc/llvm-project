//===-- Runtime thread TLS lifecycle -----------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_THREAD_TLS_H
#define LLVM_LIBC_LOADER_LINUX_THREAD_TLS_H

#include "loader/linux/runtime_scopes.h"
#include "loader/linux/tls_allocation.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

struct ThreadTLSContext {
  TLSAllocationContext startup;
  const RuntimeScopes *scopes;
};

// Service callbacks require cancellation suppression and no pthread registry
// lock. Published snapshots/images are process-resident: acquire loads pin
// their immutable metadata for this profile without a reclamation reference
// count. Prepare precedes clone and registry insertion. An error may retain
// ownership; release must complete before the descriptor is discarded or
// reused.
long prepare_runtime_thread_tls(void *, void *, ThreadTLSAllocation *);
// Call only for an unpublished worker or after terminal/reference drain. Copies
// of the descriptor are borrowed until exclusive reclaimer ownership transfers.
long release_runtime_thread_tls(void *, ThreadTLSAllocation *);
// The current DTV identifies stable loader state, not a copied pthread
// descriptor.
void *resolve_runtime_thread_tls(void *, const TLSIndex *);
// Signal-safe, nonallocating lookup. Misses must use the guarded slow resolver;
// this does not grow/refresh a DTV or make an unallocated module signal-safe.
void *try_resolve_runtime_thread_tls(void *, const TLSIndex *);
// Current-thread inspection only: never grows the DTV or instantiates a module.
void *peek_runtime_thread_tls(void *, const StartupTLSModule *);

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
