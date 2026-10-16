//===-- Resident runtime loading services ------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_RUNTIME_API_H
#define LLVM_LIBC_LOADER_LINUX_RUNTIME_API_H

#include "loader/linux/runtime_initialization.h"
#include "loader/linux/runtime_load.h"
#include "loader/linux/runtime_queries.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

// Post-adoption service owner. Registry, scopes, TLS, startup completion order,
// stack and search environment are already prepared and remain resident.
// Startup and late initialization share this mutation/finalization owner.
class RuntimeAPI {
  struct Request {
    RuntimeLoad load;
    RuntimeScopes::Transaction scope;
    RuntimeInitializers::Batch initialization;
    explicit Request(Address page)
        : load(page), scope(page), initialization(page) {}
    long reset();
  };
  Address page;
  Arena resident;
  LoaderLocks locks;
  ImageRegistry &images;
  RuntimeScopes &scopes;
  RuntimeInitializers &initializers;
  const InitialStack &stack;
  const SearchEnvironment &environment;
  StartupTLSResolver tls;
  Request *pending = nullptr;
  RuntimeScopes::Transaction closing;
  RuntimeQueries queries;
  RuntimeQueries::PeekTLS peek_tls = nullptr;
  RuntimeInitializers::Invoker invoke;
  struct ImagePin {
    uint64_t token = 0;
    const ImageRegistry::Image *images[2] = {};
    ModuleOwnership::Token owners[2] = {};
    ImagePin *next = nullptr;
  };
  ImagePin *pins = nullptr;
  RegistrySequence pin_ids;
  long retain_image(uintptr_t, uintptr_t, LoaderCancellation, uint64_t *,
                    bool process = false);
  long release_image(uint64_t, LoaderCancellation);
  long open(const char *, int, uintptr_t, LoaderCancellation, uint64_t *);
  long lookup(uint64_t, const char *, uintptr_t, LoaderCancellation,
              uintptr_t *);
  long close(uint64_t, LoaderCancellation);
  long address(uintptr_t, void *, LoaderCancellation);
  long info(uint64_t, int, void *, LoaderCancellation);
  long iterate(int (*)(void *, void *), void *, LoaderCancellation, int *,
               LoaderQueryLease *);
  long refresh_queries();

public:
  RuntimeAPI(Address page, ImageRegistry &images, RuntimeScopes &scopes,
             RuntimeInitializers &initializers, const InitialStack &stack,
             const SearchEnvironment &environment, StartupTLSResolver tls,
             RuntimeInitializers::Invoker invoke = nullptr,
             RuntimeQueries::PeekTLS peek_tls = nullptr,
             r_debug *debug = nullptr)
      : page(page), resident(page), images(images), scopes(scopes),
        initializers(initializers), stack(stack), environment(environment),
        tls(tls), closing(page), queries(page, debug), peek_tls(peek_tls),
        invoke(invoke) {}
  LoaderAPI services();
  LoaderQueryAPI query_services();
  LoaderProcessAPI process_services();
  LoaderImageAPI image_services();
  long run_startup(RuntimeInitializers::Batch &, LoaderCancellation);
  long finalize(LoaderCancellation);
  RuntimeQueries &query_metadata() { return queries; }
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
