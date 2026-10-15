//===-- Resident loader service composition ----------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_RUNTIME_STATE_H
#define LLVM_LIBC_LOADER_LINUX_RUNTIME_STATE_H

#include "loader/linux/graph.h"
#include "loader/linux/runtime_api.h"
#include "loader/linux/thread_tls.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {
r_debug *runtime_debug_record();

// One process-lifetime owner, constructed before RELRO and published before
// libc adoption. The freestanding startup path does not instantiate it.
class RuntimeState {
  Arena storage;
  ImageRegistry images;
  ImageRegistry::Transaction image_tx;
  RuntimeScopes scopes;
  RuntimeScopes::Transaction scope_tx;
  RuntimeInitializers initializers;
  RuntimeInitializers::Batch batch;
  ThreadTLSContext tls;
  SearchEnvironment environment;
  RuntimeAPI api;
  StartupUnwindImages *startup_unwind = nullptr;
  bool published = false;

public:
  RuntimeState(Address page, const InitialStack &stack,
               const RelocatedTLS &relocated)
      : storage(page), images(page), image_tx(page), scopes(page),
        scope_tx(page), batch(page), tls{{&relocated, page}, &scopes},
        api(page, images, scopes, initializers, stack, environment,
            {&tls, resolve_runtime_thread_tls}, nullptr,
            peek_runtime_thread_tls, runtime_debug_record()) {}
  long prepare(const StartupGraph &, const SearchEnvironment &, Address);
  long publish();
  long reset();
  void services(LoaderServices &, StartupUnwindImages * = nullptr);
};
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
