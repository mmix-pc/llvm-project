//===-- Startup thread TLS allocation ----------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_TLS_ALLOCATION_H
#define LLVM_LIBC_LOADER_LINUX_TLS_ALLOCATION_H

#include "loader/linux/relocation.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

struct TLSAllocationContext {
  // The token, immutable layout and template mappings outlive preparation.
  const RelocatedTLS *relocated;
  Address page_size;
};

// Raw services return positive errno and never access libc errno or set TP.
// Prepare requires an empty descriptor. Failed rollback preserves mapping
// ownership with tp/cookie zero; release must be retried before reuse.
long prepare_thread_tls(void *context, void *thread_state,
                        ThreadTLSAllocation *allocation);
// Only before publication or after terminal/reference drain. Never release
// the executing thread's live TLS. Successful fields are cleared individually.
long release_thread_tls(void *context, ThreadTLSAllocation *allocation);

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
