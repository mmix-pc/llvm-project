//===-- Deferred loader retirement coordinator -------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_RUNTIME_RETIREMENT_H
#define LLVM_LIBC_LOADER_LINUX_RUNTIME_RETIREMENT_H

#include "loader/linux/runtime_initialization.h"
#include "loader/linux/runtime_queries.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {
class RuntimeLoad;

// Private coordinator. Public close/reload and physical release are separate
// integrations. The owner installs the same event queue on all producers and
// schedules drain outside loader locks, signal handlers and unwind frames.
// Scopes are the discovery authority here. Public integration must also adopt
// committed removal in the load registry before admitting another load.
class RuntimeRetirement {
public:
  using Image = ImageRegistry::Image;
  struct Removal {
    enum State { Preparing, WaitingReaders, Ready, Quarantined, Released,
                 Collecting, Reclaimed }
        state = Preparing;
    struct Resource {
      const Image *image;
      RuntimeLoad *owner;
      Address bias;
    };
    Arena arena;
    RuntimeScopes::Transaction scope;
    cpp::span<const Image *const> images;
    cpp::span<Resource> resources;
    long release_error = 0;
    Removal *next = nullptr;
    explicit Removal(size_t page) : arena(page), scope(page) {}
  };

private:
  size_t page;
  RetirementEvents &events;
  RuntimeScopes &scopes;
  RuntimeQueries &queries;
  RuntimeInitializers &initializers;
  Arena controls, scratch;
  RuntimeInitializers::FinalizationPlan plan;
  RuntimeScopes::Transaction closing;
  Removal *working = nullptr, *removed = nullptr;
  bool active = false, exiting = false;
  long roots(cpp::span<const Image *const> &);
  long prepare_removal(LoaderLocks::Session &);
  long advance_readers();

public:
  RuntimeRetirement(size_t page, RetirementEvents &events,
                    RuntimeScopes &scopes, RuntimeQueries &queries,
                    RuntimeInitializers &initializers)
      : page(page), events(events), scopes(scopes), queries(queries),
        initializers(initializers), controls(page), scratch(page), plan(page),
        closing(page) {}
  // Consume a handle under mutation serialization; callbacks are deferred.
  long close(uint64_t handle, LoaderLocks::Session &);
  long request_exit(LoaderLocks::Session &);
  // A competing/reentrant drainer only requests another pass. Never waits for
  // the current finalizer. Errors retain work; retry at an explicit safe point.
  long drain(LoaderLocks::Session &, RuntimeInitializers::Finalizer = nullptr);
  // Writer safe point, no active drainer. Owners are committed loads kept alive
  // through this operation and any retry. Never releases kernel-owned images.
  long reclaim(ImageRegistry &, cpp::span<RuntimeLoad *const> owners,
               LoaderLocks::Session &);
  // Writer-serialized borrow. Ready means readers drained, NOT unmapped.
  const Removal *removals() const { return removed; }
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
