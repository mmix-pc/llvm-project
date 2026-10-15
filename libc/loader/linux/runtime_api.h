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

namespace LIBC_NAMESPACE_DECL {
namespace loader {

// Post-adoption service owner. Registry, scopes, TLS, startup completion order,
// stack and search environment are already prepared and remain resident.
// Installed selection and process-finalization handoff are separate.
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
  RuntimeInitializers::Invoker invoke;
  long open(const char *, int, uintptr_t, LoaderCancellation, uint64_t *);
  long lookup(uint64_t, const char *, uintptr_t, LoaderCancellation,
              uintptr_t *);

public:
  RuntimeAPI(Address page, ImageRegistry &images, RuntimeScopes &scopes,
             RuntimeInitializers &initializers, const InitialStack &stack,
             const SearchEnvironment &environment, StartupTLSResolver tls,
             RuntimeInitializers::Invoker invoke = nullptr)
      : page(page), resident(page), images(images), scopes(scopes),
        initializers(initializers), stack(stack), environment(environment),
        tls(tls), invoke(invoke) {}
  LoaderAPI services();
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
