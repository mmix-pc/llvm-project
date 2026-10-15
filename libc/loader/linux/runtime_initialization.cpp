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
  return 0;
}

static void invoke_initializer(Address address, int argc, char **argv,
                               char **envp) {
  reinterpret_cast<void (*)(int, char **, char **)>(address)(argc, argv, envp);
}

long RuntimeInitializers::run(Batch &batch, LoaderLocks::Session &session,
                              const InitialStack &stack, Invoker invoke) {
  const long tid = session.callback_owner();
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
    invoke = invoke_initializer;
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
    invoke = invoke_finalizer;
  while (completed) {
    auto *record = completed;
    completed = record->previous_completion;
    record->state = Record::Finalized;
    for (size_t j = record->fini_count; j != 0; --j) {
      LoaderLocks::Session::Callback callback(session);
      invoke(record->fini[j - 1]);
    }
  }
  return 0;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
