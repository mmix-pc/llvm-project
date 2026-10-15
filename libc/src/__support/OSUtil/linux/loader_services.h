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
  // Loader-owned stable state survives copies into the terminal reclaimer.
  uint64_t runtime_owner = 0;
};

struct StartupDTV {
  uint64_t capacity;
  uint64_t generation;
  uint64_t entries;
  uint64_t runtime_owner = 0;
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

// C-shaped, same-source runtime API. Status is a positive errno, independently
// of the result address (a successful symbol may have address zero).
struct LoaderAPI {
  void *context;
  long (*open)(void *, const char *, int, uintptr_t, LoaderCancellation,
               uint64_t *);
  long (*lookup)(void *, uint64_t, const char *, uintptr_t, LoaderCancellation,
                 uintptr_t *);
  long (*close)(void *, uint64_t, LoaderCancellation);
};

// Opaque output records use the public Dl_info, link_map and dl_phdr_info ABI.
// The iteration status and the application callback result are independent.
struct LoaderQueryAPI {
  void *context;
  long (*address)(void *, uintptr_t, void *, LoaderCancellation);
  long (*info)(void *, uint64_t, int, void *, LoaderCancellation);
  long (*iterate)(void *, int (*)(void *, void *), void *, LoaderCancellation,
                  int *);
};
enum LoaderInfoRequest { InfoLinkMap = 2, InfoTLSModid = 9, InfoTLSData = 10 };

enum LoaderOpenFlag : unsigned {
  OpenLazy = 1,
  OpenNow = 2,
  OpenNoLoad = 4,
  OpenGlobal = 0x100,
  OpenNoDelete = 0x1000,
};
constexpr uint64_t LookupDefault = 0, LookupNext = ~uint64_t(0);
inline bool valid_open_flags(unsigned flags) {
  unsigned binding = flags & (OpenLazy | OpenNow);
  return (binding == OpenLazy || binding == OpenNow) &&
         !(flags & ~(OpenLazy | OpenNow | OpenNoLoad | OpenGlobal | OpenNoDelete));
}

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
  // Empty in the startup-only profile. Adopt once before application threads;
  // never turn incomplete callback pairs into apparent runtime support.
  LoaderAPI api{};
  LoaderQueryAPI queries{};
};

struct LibcBootstrap {
  uint64_t size;
  long (*prepare_main)(const StartupProcessView *, void **);
  long (*adopt_main)(const StartupProcessView *, const LoaderServices *, void *,
                     const ThreadTLSAllocation *);
  void (*abandon_main)(void *);
};
static_assert(sizeof(StartupProcessView) == 64);
static_assert(sizeof(LoaderServices) == 136);
static_assert(sizeof(LibcBootstrap) == 32);

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
