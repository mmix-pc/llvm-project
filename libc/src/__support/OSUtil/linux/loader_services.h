//===-- Private loader/runtime TLS descriptors -------------------*- C++ -*-===//
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

// Post-adoption cancellation scope. A leave callback may deliver pending
// asynchronous cancellation; the loader must release all locks first.
struct LoaderCancellation {
  long (*enter)(uint64_t *saved);
  long (*leave)(uint64_t saved);
};

struct UnwindReadableRange {
  uint64_t address;
  uint64_t size;
};

struct UnwindImageView {
  uint64_t load_bias;
  uint64_t phdr_address;
  uint64_t phdr_count;
  uint64_t eh_frame_header;
  uint64_t eh_frame_header_size;
  uint64_t readable_ranges;
  uint64_t range_count;
  uint64_t token;
};

struct TLSIndex {
  uint64_t module_id;
  uint64_t offset;
};

struct StartupTLSResolver {
  void *context;
  void *(*resolve)(void *, const TLSIndex *);
};

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

struct ThreadTLSAllocation {
  uint64_t tp;
  uint64_t tls_mapping_base;
  uint64_t tls_mapping_size;
  uint64_t dtv_mapping_base;
  uint64_t dtv_mapping_size;
  uint64_t registry_cookie;
};

struct StartupDTV {
  uint64_t capacity;
  uint64_t generation;
  uint64_t entries;
};

struct StartupDTVEntry {
  uint64_t generation;
  uint64_t image_base;
  uint64_t allocation_owner;
  uint64_t allocation_extent;
};

struct StartupProcessView {
  uint64_t original_sp, argc, argv, envp, auxv, page_size, random, entry;
};

struct LoaderServices {
  uint64_t size;
  void *context;
  const StartupTLSSet *tls_set;
  long (*prepare_thread_tls)(void *, void *, ThreadTLSAllocation *);
  long (*release_thread_tls)(void *, ThreadTLSAllocation *);
  void *(*resolve_startup_tls)(void *, const TLSIndex *);
  // Null until startup finalization is integrated; never a successful no-op.
  void (*finalize_startup)(void *);
  long (*find_unwind_image)(void *, uintptr_t, UnwindImageView *);
  void (*release_unwind_image)(void *, uintptr_t);
};

struct LibcBootstrap {
  uint64_t size;
  long (*prepare_main)(const StartupProcessView *, void **);
  long (*adopt_main)(const StartupProcessView *, const LoaderServices *, void *,
                     const ThreadTLSAllocation *);
  void (*abandon_main)(void *);
};
static_assert(sizeof(StartupProcessView) == 64);
static_assert(sizeof(LoaderServices) == 72);
static_assert(sizeof(LibcBootstrap) == 32);

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
