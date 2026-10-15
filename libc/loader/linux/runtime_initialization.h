//===-- Resident runtime initializers ----------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_RUNTIME_INITIALIZATION_H
#define LLVM_LIBC_LOADER_LINUX_RUNTIME_INITIALIZATION_H

#include "loader/linux/entry.h"
#include "loader/linux/image_registry.h"
#include "loader/linux/initialization_plan.h"
#include "loader/linux/locks.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

// All operations require the same recursive mutation guard and cancellation
// suppression. No registry/arena guard may span run/finalize. Other threads
// wait at the mutation guard, not on a partially initialized object's state.
class RuntimeInitializers {
  struct Record;
  struct Node;
  struct Edge {
    const Node *target;
    const Edge *next;
  };
  struct Node {
    const ImageRegistry::Image *source = nullptr;
    DynamicInfo dynamic;
    Node *next_node = nullptr;
    Edge *edges = nullptr;
    Record *record = nullptr;
    const ElfImage *image() const { return &source->elf; }
    const DynamicInfo *metadata() const { return &dynamic; }
    const Node *next() const { return next_node; }
    const Edge *dependencies() const { return edges; }
  };

public:
  class Batch {
    friend class RuntimeInitializers;
    Arena arena;
    RuntimeInitializers *owner = nullptr;
    const ImageRegistry::Snapshot *registry = nullptr;
    Node *nodes = nullptr;
    InitializationPlan<Node> plan;
    uint64_t epoch = 0;
    bool attached = false, running = false;
    bool startup = false, preinitialized = false;
    long runner = 0;

  public:
    explicit Batch(size_t page_size) : arena(page_size) {}
    Batch(const Batch &) = delete;
    Batch &operator=(const Batch &) = delete;
    // Attached plans, callback snapshots and records stay process-resident.
    long abort();
  };
  using Invoker = void (*)(Address, int, char **, char **);
  using Finalizer = void (*)(Address);

private:
  struct Record {
    enum State { Pending, Initializing, Complete, Finalized } state = Pending;
    const ImageRegistry::Image *image;
    Record *next = nullptr, *previous_completion = nullptr;
    const Address *fini = nullptr;
    size_t fini_count = 0;
    Batch *batch = nullptr;
    long tid = 0;
  };
  Record *records = nullptr, *completed = nullptr;
  uint64_t epoch = 0;
  bool stopping = false;
  Record *find(const ImageRegistry::Image *) const;

public:
  // Prepare before publishing mappings. Attachment is allocation-free, after
  // matching scope publication and before any constructor can reenter.
  long prepare(const ImageRegistry::Snapshot &, Batch &, bool startup = false);
  long attach(Batch &, const ImageRegistry::Snapshot &published);
  // Once-only handoff from a fully completed startup scheduler, in its actual
  // completion order. Do not run startup callbacks twice or keep two fini
  // owners.
  long adopt_startup(Batch &, cpp::span<const ImageRegistry::Image *const>);
  // Callbacks must return normally or terminate the process. Escaping
  // exceptions, pthread_exit and overriding cancellation suppression are not
  // recoverable loader transactions; the service boundary must enforce this.
  long run(Batch &, LoaderLocks::Session &, const InitialStack &,
           Invoker = nullptr);
  long finalize(LoaderLocks::Session &, Finalizer = nullptr);
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
