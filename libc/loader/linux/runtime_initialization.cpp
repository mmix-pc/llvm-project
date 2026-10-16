//===-- Resident runtime initializer scheduling ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/runtime_initialization.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

template <class T> static long allocate(Arena &arena, size_t n, T *&out) {
  out = nullptr;
  if (!n)
    return 0;
  if (n > __SIZE_MAX__ / sizeof(T))
    return -EOVERFLOW;
  void *storage;
  long error = arena.allocate(n * sizeof(T), alignof(T), storage);
  if (!error)
    out = static_cast<T *>(storage);
  return error;
}

RuntimeInitializers::Record *
RuntimeInitializers::find(const ImageRegistry::Image *image) const {
  for (auto *record = records; record; record = record->next)
    if (record->image == image)
      return record;
  return nullptr;
}

long RuntimeInitializers::Batch::abort() {
  if (attached)
    return -EBUSY;
  long error = arena.reset();
  if (!error) {
    owner = nullptr;
    registry = nullptr;
    nodes = nullptr;
    plan = {};
  }
  return error;
}

long RuntimeInitializers::prepare(const ImageRegistry::Snapshot &registry,
                                  Batch &batch, bool startup) {
  if (stopping)
    return -ECANCELED;
  if (batch.owner)
    return -EBUSY;
  if (epoch == ~uint64_t(0))
    return -EOVERFLOW;
  batch.owner = this;
  batch.startup = startup;
  batch.epoch = epoch;
  const size_t n = registry.images.size();
  if (!n)
    return -EINVAL;
  long error = allocate(batch.arena, n, batch.nodes);
  if (error)
    return error;
  for (size_t i = 0; i < n; ++i) {
    auto &node = *new (batch.nodes + i) Node;
    node.source = registry.images[i];
    if (!node.source || !node.source->discovered)
      return -EINVAL;
    for (size_t j = 0; j < i; ++j)
      if (registry.images[j] == node.source)
        return -EINVAL;
    node.next_node = i + 1 < n ? batch.nodes + i + 1 : nullptr;
    if (DynamicInfo::open(node.source->elf, node.dynamic) != DynamicError::None)
      return -ENOEXEC;
    node.record = find(node.source);
    if (!node.record) {
      error = allocate(batch.arena, 1, node.record);
      if (error)
        return error;
      new (node.record) Record{Record::Pending, node.source};
    }
    const auto dependencies = node.source->dependencies;
    error = allocate(batch.arena, dependencies.size(), node.edges);
    if (error)
      return error;
    for (size_t j = 0; j < dependencies.size(); ++j) {
      size_t target = 0;
      while (target < n && registry.images[target] != dependencies[j])
        ++target;
      if (target == n)
        return -EINVAL;
      new (node.edges + j)
          Edge{batch.nodes + target,
               j + 1 < dependencies.size() ? node.edges + j + 1 : nullptr};
    }
  }
  struct Graph {
    using Edge = RuntimeInitializers::Edge;
    Node *nodes;
    size_t count;
    const Node *scope() const { return nodes; }
    size_t size() const { return count; }
  };
  error = batch.plan.prepare(Graph{batch.nodes, n}, batch.arena, startup);
  if (error)
    return error;
  batch.registry = &registry;
  return 0;
}

long RuntimeInitializers::attach(Batch &batch,
                                 const ImageRegistry::Snapshot &published) {
  if (stopping)
    return -ECANCELED;
  if (batch.owner != this || batch.registry != &published || batch.attached)
    return -EINVAL;
  if (batch.epoch != epoch)
    return -EAGAIN;
  for (size_t i = 0; i < batch.plan.count; ++i) {
    const auto &object = batch.plan.order[i];
    auto *record = object.node->record;
    if (!find(record->image)) {
      record->storage_owner = &batch;
      ++batch.owned_records;
      record->ownership_arena = &batch.arena;
      record->fini = object.fini_count
                         ? batch.plan.callbacks + object.fini_first
                         : nullptr;
      record->fini_count = object.fini_count;
      record->next = records;
      records = record;
    }
  }
  ++epoch;
  batch.attached = true;
  batch.next = batches;
  batches = &batch;
  return 0;
}

long RuntimeInitializers::adopt_startup(
    Batch &batch, cpp::span<const ImageRegistry::Image *const> order) {
  if (stopping || completed || epoch != 1 || !batch.attached ||
      batch.owner != this || order.size() != batch.plan.count)
    return -EINVAL;
  for (size_t i = 0; i < order.size(); ++i) {
    auto *record = find(order[i]);
    if (!record || !record->image->startup || record->state != Record::Pending)
      return -EINVAL;
    for (size_t j = 0; j < i; ++j)
      if (order[j] == order[i])
        return -EINVAL;
  }
  for (auto *image : order) {
    auto *record = find(image);
    record->state = Record::Complete;
    record->previous_completion = completed;
    completed = record;
  }
  batch.finished = true;
  return 0;
}

static void invoke_initializer(Address address, int argc, char **argv,
                               char **envp) {
  reinterpret_cast<void (*)(int, char **, char **)>(address)(argc, argv, envp);
}

long RuntimeInitializers::run(Batch &batch, LoaderLocks::Session &session,
                              const InitialStack &stack, Invoker invoke) {
  const long tid = session.callback_owner();
  if (batch.released)
    return -EBUSY;
  if (!tid || batch.owner != this || !batch.attached || !stack.valid ||
      stack.argc > __INT_MAX__)
    return -EINVAL;
  if (stopping)
    return -ECANCELED;
  if (batch.running)
    return batch.runner == tid ? 0 : -EBUSY;
  for (size_t i = 0; i < batch.plan.count; ++i) {
    auto *record = batch.plan.order[i].node->record;
    if ((record->state == Record::Initializing && record->tid != tid) ||
        (record->state == Record::Pending && record->image->startup &&
         !batch.startup))
      return -EBUSY;
  }
  batch.running = true;
  batch.runner = tid;
  // Claim the entire pending plan before callbacks, so a recursive open of a
  // cycle or its still-pending siblings cannot initialize them out of order.
  for (size_t i = 0; i < batch.plan.count; ++i) {
    auto *record = batch.plan.order[i].node->record;
    if (record->state == Record::Pending) {
      record->state = Record::Initializing;
      record->tid = tid;
      record->batch = &batch;
    }
  }
  if (!invoke)
    invoke = session.initializer_barrier() ? session.initializer_barrier()
                                           : invoke_initializer;
  if (batch.startup && !batch.preinitialized) {
    batch.preinitialized = true;
    for (size_t j = 0; j < batch.plan.preinit_count && !stopping; ++j) {
      LoaderLocks::Session::Callback callback(session);
      invoke(batch.plan.callbacks[j], static_cast<int>(stack.argc),
             reinterpret_cast<char **>(stack.argv),
             reinterpret_cast<char **>(stack.envp));
    }
  }
  for (size_t i = 0; i < batch.plan.count && !stopping; ++i) {
    const auto &object = batch.plan.order[i];
    auto *record = object.node->record;
    if (record->state != Record::Initializing || record->batch != &batch)
      continue;
    for (size_t j = 0; j < object.count && !stopping; ++j) {
      LoaderLocks::Session::Callback callback(session);
      invoke(batch.plan.callbacks[object.first + j],
             static_cast<int>(stack.argc),
             reinterpret_cast<char **>(stack.argv),
             reinterpret_cast<char **>(stack.envp));
    }
    if (!stopping) {
      record->state = Record::Complete;
      record->previous_completion = completed;
      completed = record;
    }
  }
  batch.running = false;
  batch.finished = true;
  return stopping ? -ECANCELED : 0;
}

static void invoke_finalizer(Address address) {
  reinterpret_cast<void (*)()>(address)();
}

long RuntimeInitializers::finalize(LoaderLocks::Session &session,
                                   Finalizer invoke) {
  if (!session.callback_owner())
    return -EINVAL;
  if (stopping)
    return 0;
  stopping = true;
  if (!invoke)
    invoke = session.finalizer_barrier() ? session.finalizer_barrier()
                                        : invoke_finalizer;
  while (completed) {
    auto *record = completed;
    completed = record->previous_completion;
    if (record->state != Record::Complete)
      continue;
    record->state = Record::Finalized;
    for (size_t j = record->fini_count; j != 0; --j) {
      LoaderLocks::Session::Callback callback(session);
      invoke(record->fini[j - 1]);
    }
  }
  return 0;
}

long RuntimeInitializers::FinalizationPlan::release_pins() {
  while (pins) {
    long error = pins->image->ownership.release(pins->token);
    if (error)
      return error;
    pins = pins->next;
  }
  return 0;
}

long RuntimeInitializers::FinalizationPlan::reset() {
  if ((state != Empty && state != Done) || pins)
    return -EBUSY;
  owner = nullptr;
  items = nullptr;
  runner = 0;
  state = Empty;
  return arena.reset();
}

long RuntimeInitializers::collect_finalizers(
    const ImageRegistry::Snapshot &snapshot, FinalizationPlan &plan,
    LoaderLocks::Session &session, bool process_exit,
    cpp::span<const ImageRegistry::Image *const> external_roots) {
  if (!session.callback_owner() || plan.owner ||
      plan.state != FinalizationPlan::Empty)
    return -EINVAL;
  if (stopping && !process_exit)
    return -ECANCELED;
  cpp::span<const ImageRegistry::Image *const> candidates = snapshot.images;
  long error = 0;
  if (!process_exit) {
    const ImageRegistry::Image **roots = nullptr;
    if (external_roots.size() > __SIZE_MAX__ - snapshot.images.size())
      return -EOVERFLOW;
    error = allocate(plan.arena, snapshot.images.size() + external_roots.size(),
                     roots);
    size_t count = 0;
    if (!error) {
      for (const auto *image : external_roots)
        roots[count++] = image;
      for (auto *image : snapshot.images) {
        auto *record = find(image);
        // An unfinished initializer can still call its completed providers.
        if (!record || record->state == Record::Pending ||
            record->state == Record::Initializing)
          roots[count++] = image;
      }
      error = snapshot.reclaimable(plan.arena, {roots, count}, candidates, true);
    }
  }
  auto pin = [&](const ImageRegistry::Image *image) -> long {
    for (auto *p = plan.pins; p; p = p->next)
      if (p->image == image)
        return 0;
    auto *record = find(image);
    if (!record || !record->ownership_arena)
      return -EINVAL;
    FinalizationPlan::Pin *p;
    long result = allocate(plan.arena, 1, p);
    if (result)
      return result;
    new (p) FinalizationPlan::Pin{image, {}, nullptr};
    result = image->ownership.acquire(*record->ownership_arena,
                                      ModuleOwnership::Kind::Runtime, p->token);
    if (!result) {
      p->next = plan.pins;
      plan.pins = p;
    }
    return result;
  };
  auto **tail = &plan.items;
  // Completion order already incorporates dependency-first SCC scheduling
  // and nested initialization. Never finalize an unfinished constructor.
  for (auto *record = completed; record && !error;
       record = record->previous_completion) {
    bool selected = false;
    for (auto *image : candidates)
      selected |= record->image == image;
    if (!selected || record->state != Record::Complete)
      continue;
    if (!record->image->ownership.available_for_load()) {
      error = -EBUSY;
      break;
    }
    FinalizationPlan::Item *item;
    error = allocate(plan.arena, 1, item);
    if (error)
      break;
    new (item) FinalizationPlan::Item{record, nullptr};
    *tail = item;
    tail = &item->next;
    error = pin(record->image);
    for (size_t i = 0; i < record->fini_count && !error; ++i) {
      const ImageRegistry::Image *provider = nullptr;
      for (auto *image : snapshot.images) {
        Address address;
        Address pc = record->fini[i];
        if (pc >= image->elf.load_bias() &&
            image->elf.runtime_address(pc - image->elf.load_bias(), 4,
                                       PF_R | PF_X, address)) {
          if (provider) {
            error = -EINVAL;
            break;
          }
          provider = image;
        }
      }
      if (!error)
        error = provider ? pin(provider) : -ENOEXEC;
    }
  }
  if (error) {
    if (plan.release_pins())
      __builtin_trap();
    // Preserve arena ownership for explicit reset even if unmapping fails.
    plan.items = nullptr;
    return error;
  }
  for (auto *item = plan.items; item; item = item->next) {
    item->record->image->ownership.begin_finalization();
    item->record->state = Record::Finalizing;
  }
  if (process_exit)
    stopping = true;
  plan.owner = this;
  plan.runner = session.callback_owner();
  plan.state = FinalizationPlan::Prepared;
  return 0;
}

long RuntimeInitializers::finalization_candidates(
    const FinalizationPlan &plan, const ImageRegistry::Snapshot &snapshot,
    Arena &scratch, cpp::span<const ImageRegistry::Image *const> roots,
    cpp::span<const ImageRegistry::Image *const> &out,
    LoaderLocks::Session &session) const {
  out = {};
  if (!session.callback_owner() || plan.owner != this)
    return -EINVAL;
  if (plan.state != FinalizationPlan::Done)
    return -EBUSY;
  cpp::span<const ImageRegistry::Image *const> eligible;
  long error = retirement_candidates(snapshot, scratch, roots, eligible, session);
  if (error)
    return error;
  const ImageRegistry::Image **selected;
  error = allocate(scratch, eligible.size(), selected);
  if (error)
    return error;
  size_t count = 0;
  for (const auto *image : eligible)
    for (auto *item = plan.items; item; item = item->next)
      if (item->record->image == image) {
        selected[count++] = image;
        break;
      }
  out = {selected, count};
  return 0;
}

long RuntimeInitializers::retirement_candidates(
    const ImageRegistry::Snapshot &snapshot, Arena &scratch,
    cpp::span<const ImageRegistry::Image *const> roots,
    cpp::span<const ImageRegistry::Image *const> &out,
    LoaderLocks::Session &session) const {
  out = {};
  if (!session.callback_owner())
    return -EINVAL;
  // Completed plans released their invocation pins. Never wait for an active
  // callback's pins, nor reuse a pre-callback reachability result.
  if (roots.size() > ~size_t(0) - snapshot.images.size())
    return -EOVERFLOW;
  const ImageRegistry::Image **current_roots;
  long error = allocate(scratch, roots.size() + snapshot.images.size(),
                        current_roots);
  if (error)
    return error;
  size_t root_count = 0;
  for (const auto *image : roots)
    current_roots[root_count++] = image;
  for (const auto *image : snapshot.images) {
    const auto *record = find(image);
    if (!record || record->state == Record::Pending ||
        record->state == Record::Initializing)
      current_roots[root_count++] = image;
  }
  cpp::span<const ImageRegistry::Image *const> eligible;
  error = snapshot.reclaimable(scratch, {current_roots, root_count}, eligible);
  if (error)
    return error;
  const ImageRegistry::Image **selected;
  error = allocate(scratch, eligible.size(), selected);
  if (error)
    return error;
  size_t count = 0;
  for (const auto *image : eligible)
    if (const auto *record = find(image);
        record && record->state == Record::Finalized)
      selected[count++] = image;
  out = {selected, count};
  return 0;
}

long RuntimeInitializers::invoke_finalizers(FinalizationPlan &plan,
                                           LoaderLocks::Session &session,
                                           Finalizer invoke) {
  if (plan.owner != this || !session.unlocked_callback_owner() ||
      plan.runner != session.unlocked_callback_owner())
    return -EINVAL;
  if (plan.state == FinalizationPlan::Invoked ||
      plan.state == FinalizationPlan::Done)
    return 0;
  if (plan.state != FinalizationPlan::Prepared)
    return -EBUSY;
  plan.state = FinalizationPlan::Running;
  if (!invoke)
    invoke = session.finalizer_barrier() ? session.finalizer_barrier()
                                        : invoke_finalizer;
  for (auto *item = plan.items; item; item = item->next) {
    auto *record = item->record;
    // The CRT's fini-array entry invokes __cxa_finalize(__dso_handle).
    // Do not separately drain the same process registrations here.
    for (size_t j = record->fini_count; j; --j) {
      LoaderLocks::Session::Callback callback(session, true);
      invoke(record->fini[j - 1]);
    }
  }
  plan.state = FinalizationPlan::Invoked;
  return 0;
}

long RuntimeInitializers::complete_finalizers(FinalizationPlan &plan,
                                             LoaderLocks::Session &session) {
  if (!session.callback_owner() || plan.owner != this ||
      plan.runner != session.callback_owner())
    return -EINVAL;
  if (plan.state == FinalizationPlan::Done)
    return 0;
  if (plan.state != FinalizationPlan::Invoked)
    return -EBUSY;
  for (auto *item = plan.items; item; item = item->next)
    item->record->state = Record::Finalized;
  if (plan.release_pins())
    __builtin_trap();
  plan.state = FinalizationPlan::Done;
  return 0;
}

long RuntimeInitializers::collect_reclaimed(LoaderLocks::Session &session) {
  if (!session.callback_owner())
    return -EINVAL;
  for (auto **link = &completed; *link;) {
    auto *record = *link;
    if (record->image->ownership.admission() == ModuleOwnership::State::Reclaimed)
      *link = record->previous_completion;
    else
      link = &record->previous_completion;
  }
  for (auto **link = &records; *link;) {
    auto *record = *link;
    if (record->image->ownership.admission() != ModuleOwnership::State::Reclaimed) {
      link = &record->next;
      continue;
    }
    if (record->state != Record::Finalized || !record->storage_owner ||
        !record->storage_owner->owned_records)
      __builtin_trap();
    --record->storage_owner->owned_records;
    *link = record->next;
  }
  long error = 0;
  for (auto *batch = batches; batch; batch = batch->next) {
    if (batch->startup || !batch->finished || batch->running || batch->owned_records)
      continue;
    batch->released = true;
    batch->registry = nullptr;
    batch->nodes = nullptr;
    long result = batch->arena.reset();
    if (!error)
      error = result;
  }
  return error;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
