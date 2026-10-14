//===-- Private loader/runtime TLS descriptors -------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_LOADER_SERVICES_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_LOADER_SERVICES_H

#include "hdr/stdint_proxy.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

struct StartupTLSModule {
  uint64_t module_id;
  uint64_t generation;
  uint64_t template_address;
  uint64_t file_size;
  uint64_t memory_size;
  uint64_t alignment;
  uint64_t residue;
  uint64_t tp_offset;
};

// A planned set is not yet an adopted runtime service. Template relocation
// must finish before any thread copies these bytes or publishes its TP.
struct StartupTLSSet {
  uint64_t generation;
  uint64_t module_count;
  const StartupTLSModule *modules;
  uint64_t tp_alignment;
  uint64_t tls_extent;
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
