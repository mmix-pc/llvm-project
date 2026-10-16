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

// State operations require mutation serialization and cancellation suppression.
// Legacy run/finalize retain mutation but no registry/arena guard. Detached
// finalization invokes callbacks without any loader guard.
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
    enum State { Pending, Initializing, Complete, Finalizing, Finalized } state =
        Pending;
    const ImageRegistry::Image *image;
    Record *next = nullptr, *previous_completion = nullptr;
    const Address *fini = nullptr;
    size_t fini_count = 0;
    Batch *batch = nullptr;
    Arena *ownership_arena = nullptr;
    long tid = 0;
  };
  Record *records = nullptr, *completed = nullptr;
  uint64_t epoch = 0;
  bool stopping = false;
  Record *find(const ImageRegistry::Image *) const;

public:
  // A single-thread-owned detached plan. Its arena may be reset only after
  // completion or failed preparation; ownership slots use the resident batch
  // arena, never this temporary storage.
  class FinalizationPlan {
    friend class RuntimeInitializers;
    struct Item {
      Record *record;
      Item *next;
    };
    struct Pin {
      const ImageRegistry::Image *image;
      ModuleOwnership::Token token;
      Pin *next;
    };
    Arena arena;
    RuntimeInitializers *owner = nullptr;
    Item *items = nullptr;
    Pin *pins = nullptr;
    long runner = 0;
    enum State { Empty, Prepared, Running, Invoked, Done } state = Empty;
    long release_pins();

  public:
    explicit FinalizationPlan(size_t page_size) : arena(page_size) {}
    FinalizationPlan(const FinalizationPlan &) = delete;
    FinalizationPlan &operator=(const FinalizationPlan &) = delete;
    long reset();
  };
  // Collection and completion require mutation serialization. The snapshot
  // and attached initializer batches must remain alive through completion.
  // Process exit includes permanent images; ordinary collection uses graph
  // reachability, so NODELETE and externally held dependency cycles stay live.
  long collect_finalizers(const ImageRegistry::Snapshot &, FinalizationPlan &,
                          LoaderLocks::Session &, bool process_exit = false);
  long invoke_finalizers(FinalizationPlan &, LoaderLocks::Session &,
                         Finalizer = nullptr);
  long complete_finalizers(FinalizationPlan &, LoaderLocks::Session &);

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
