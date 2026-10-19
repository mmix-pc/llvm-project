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
  void (*begin_callback)() = nullptr;
  void (*end_callback)() = nullptr;
  // Optional runtime-owned exception barriers; the interpreter itself has no
  // C++ exception runtime dependency. Rebuild both protocol owners together.
  void (*invoke_initializer)(uintptr_t, int, char **, char **) = nullptr;
  void (*invoke_finalizer)(uintptr_t) = nullptr;
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
  // Signal-safe lookup of already allocated TLS; null means slow path needed.
  void *(*try_resolve)(void *, const TLSIndex *) = nullptr;
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
// The libc caller owns cleanup across application callbacks, including forced
// unwind. The interpreter has no C++ exception personality or cleanup runtime.
struct LoaderQueryLease {
  void *token = nullptr;
  void (*release)(void *) = nullptr;
};

struct LoaderQueryAPI {
  void *context;
  long (*address)(void *, uintptr_t, void *, LoaderCancellation);
  long (*info)(void *, uint64_t, int, void *, LoaderCancellation);
  long (*iterate)(void *, int (*)(void *, void *), void *, LoaderCancellation,
                  int *, LoaderQueryLease *);
};
enum LoaderInfoRequest { InfoLinkMap = 2, InfoTLSModid = 9, InfoTLSData = 10 };

struct LoaderProcessAPI {
  void *context;
  // Quiescence alone is insufficient: libc must also prove a single-thread
  // snapshot before asking the child to resume loader finalization.
  long (*prepare_fork)(void *, bool *quiescent);
  void (*finish_fork)(void *, bool child, bool resume);
};

// Same-source ownership service. Tokens are single-owner and never reused.
// The caller suppresses cancellation through retain and publication/rollback,
// and through callback completion and release. No lock spans the callback.
struct LoaderImageAPI {
  void *context;
  long (*retain)(void *, uintptr_t dso, uintptr_t callback, LoaderCancellation,
                 uint64_t *token);
  long (*release)(void *, uint64_t token, LoaderCancellation);
  // DSO-owned process callback; a distinct callback image remains an external
  // runtime owner until invocation completes. Release uses the same service.
  long (*retain_process)(void *, uintptr_t dso, uintptr_t callback,
                        LoaderCancellation, uint64_t *token) = nullptr;
};

// Single-owner stack record; never copy or expose to application code.
struct LoaderSignalLease {
  void *snapshot = nullptr;
  const void *images[2] = {};
};
struct LoaderSignalAPI {
  void *context = nullptr;
  long (*retain)(void *, uintptr_t handler, uintptr_t restorer,
                 LoaderSignalLease *) = nullptr;
  void (*finish)(void *, LoaderSignalLease *, bool installed) = nullptr;
};

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

// Private same-source composition contract, not a public ABI version.
enum class LoaderLifetime : uint64_t { Unspecified, Reclaiming };

struct LoaderServices {
  uint64_t size;
  void *context;
  const StartupTLSSet *tls_set;
  long (*prepare_thread_tls)(void *, void *, ThreadTLSAllocation *);
  long (*release_thread_tls)(void *, ThreadTLSAllocation *);
  void *(*resolve_startup_tls)(void *, const TLSIndex *);
  // Null until startup finalization is integrated; never a successful no-op.
  void (*finalize_startup)(void *, LoaderCancellation);
  long (*find_unwind_image)(void *, uintptr_t, UnwindImageView *);
  void (*release_unwind_image)(void *, uintptr_t);
  // Empty in the startup-only profile. Adopt once before application threads;
  // never turn incomplete callback pairs into apparent runtime support.
  LoaderAPI api{};
  LoaderQueryAPI queries{};
  LoaderProcessAPI process{};
  long (*run_initializers)(void *, LoaderCancellation) = nullptr;
  LoaderImageAPI images{};
  LoaderSignalAPI signals{};
  // Independent checked snapshot owners; resident queries above remain for
  // cursors whose copy/destruction lifetime has not yet been integrated.
  long (*acquire_unwind_image)(void *, uintptr_t, UnwindImageView *) = nullptr;
  long (*retain_unwind_image)(void *, uintptr_t, UnwindImageView *) = nullptr;
  long (*drop_unwind_image)(void *, uintptr_t) = nullptr;
  long (*lookup_unwind_lease)(void *, uintptr_t, uintptr_t,
                              UnwindImageView *) = nullptr;
  long (*pin_unwind_lease)(void *, uintptr_t) = nullptr;
  LoaderLifetime lifetime = LoaderLifetime::Unspecified;
  void *(*try_resolve_tls)(void *, const TLSIndex *) = nullptr;
};

inline bool has_reclaiming_services(const LoaderServices &s) {
  // Check size before reading the new tail of an older provider descriptor.
  return s.size == sizeof(s) && s.lifetime == LoaderLifetime::Reclaiming &&
         s.context && s.tls_set && s.prepare_thread_tls && s.release_thread_tls &&
         s.resolve_startup_tls && s.finalize_startup && s.find_unwind_image &&
         s.release_unwind_image && s.acquire_unwind_image &&
         s.retain_unwind_image && s.drop_unwind_image && s.lookup_unwind_lease &&
         s.pin_unwind_lease && s.api.context && s.api.open && s.api.lookup &&
         s.api.close && s.queries.context && s.queries.address && s.queries.info &&
         s.queries.iterate && s.process.context && s.process.prepare_fork &&
         s.process.finish_fork && s.run_initializers && s.images.context &&
         s.images.retain && s.images.release && s.images.retain_process &&
         s.signals.context && s.signals.retain && s.signals.finish &&
         s.try_resolve_tls;
}

struct LibcBootstrap {
  uint64_t size;
  long (*prepare_main)(const StartupProcessView *, void **);
  long (*adopt_main)(const StartupProcessView *, const LoaderServices *, void *,
                     const ThreadTLSAllocation *);
  void (*abandon_main)(void *);
};
static_assert(sizeof(StartupProcessView) == 64);
static_assert(sizeof(LoaderServices) == 280);
static_assert(sizeof(LibcBootstrap) == 32);

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
