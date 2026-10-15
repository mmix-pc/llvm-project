//===-- Resident loader service composition -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/runtime_state.h"
#include "loader/linux/runtime_unwind.h"
#include "src/__support/CPP/new.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {
template <class T> static long allocate(Arena &arena, size_t n, T *&out) {
  if (!n) {
    out = nullptr;
    return 0;
  }
  if (n > __SIZE_MAX__ / sizeof(T))
    return -EOVERFLOW;
  void *p;
  long error = arena.allocate(n * sizeof(T), alignof(T), p);
  if (!error) {
    out = static_cast<T *>(p);
    for (size_t i = 0; i < n; ++i)
      new (out + i) T{};
  }
  return error;
}

long RuntimeState::prepare(const StartupGraph &graph,
                           const SearchEnvironment &search, Address bias) {
  // Preserve the initial search policy even if application code edits environ.
  unsigned char *paths, *origin;
  constexpr char prefix[] = "LD_LIBRARY_PATH=";
  long error =
      allocate(storage, sizeof(prefix) - 1 + search.paths().size(), paths);
  if (error)
    return error;
  for (size_t i = 0; i < sizeof(prefix) - 1; ++i)
    paths[i] = prefix[i];
  for (size_t i = 0; i < search.paths().size(); ++i)
    paths[sizeof(prefix) - 1 + i] = search.paths()[i];
  error = allocate(storage, search.main_origin().size(), origin);
  if (error)
    return error;
  for (size_t i = 0; i < search.main_origin().size(); ++i)
    origin[i] = search.main_origin()[i];
  Bytes env(paths, sizeof(prefix) - 1 + search.paths().size());
  error = environment.parse(false, {&env, 1},
                            {origin, search.main_origin().size()});
  if (error)
    return error;
  size_t n = graph.size();
  const ElfImage **elf;
  const StartupGraph::Node **nodes;
  ImageRegistry::Discovery *discovery;
  if ((error = allocate(storage, n, elf)) ||
      (error = allocate(storage, n, nodes)) ||
      (error = allocate(storage, n, discovery)))
    return error;
  size_t i = 0;
  for (auto *node = graph.scope(); node; node = node->next()) {
    if (i == n)
      return -EINVAL;
    nodes[i] = node;
    elf[i++] = node->image();
  }
  if (!n || i != n)
    return -EINVAL;
  for (i = 0; i < n; ++i) {
    auto &d = discovery[i];
    auto *node = nodes[i];
    if (const auto *id = node->file_identity()) {
      d.identity_known = true;
      d.identity = *id;
    }
    d.path = node->path();
    for (size_t j = 0; j < n; ++j)
      if (nodes[j] == node->discovery_parent())
        d.parent = j;
    size_t count = 0;
    for (auto *edge = node->dependencies(); edge; edge = edge->next)
      ++count;
    size_t *edges;
    if ((error = allocate(storage, count, edges)))
      return error;
    size_t k = 0;
    for (auto *edge = node->dependencies(); edge; edge = edge->next) {
      size_t j = 0;
      while (j < n && nodes[j] != edge->target)
        ++j;
      if (j == n)
        return -EINVAL;
      edges[k++] = j;
    }
    d.dependencies = {edges, count};
  }
  if ((error = images.prepare(image_tx, {elf, n}, {discovery, n})))
    return error;
  return api.query_metadata().bind_debug(*elf[0], bias);
}

long RuntimeState::publish() {
  if (!image_tx.mark_relocated() || !image_tx.mark_tls_ready() ||
      !image_tx.mark_protected())
    return -EINVAL;
  long error = images.publish(image_tx);
  if (!error)
    published = true;
  if (!error)
    error = scopes.prepare_startup(*images.snapshot(), scope_tx);
  if (!error)
    error = scopes.prepare_unwind(scope_tx);
  if (!error)
    error = scopes.publish(scope_tx);
  if (!error)
    error = initializers.prepare(*images.snapshot(), batch, true);
  if (!error)
    error = initializers.attach(batch, *images.snapshot());
  if (!error)
    error = api.query_metadata().prepare(*images.snapshot());
  if (!error)
    api.query_metadata().publish();
  return error;
}

long RuntimeState::reset() {
  if (published)
    return -EBUSY;
  long error = batch.abort();
  if (!error)
    error = scope_tx.abort();
  if (!error)
    error = image_tx.abort();
  if (!error)
    error = api.query_metadata().abort();
  return error ? error : storage.reset();
}

void RuntimeState::services(LoaderServices &out, StartupUnwindImages *unwind) {
  startup_unwind = unwind;
  out.context = this;
  out.prepare_thread_tls = [](void *p, void *state, ThreadTLSAllocation *a) {
    return prepare_runtime_thread_tls(&static_cast<RuntimeState *>(p)->tls,
                                      state, a);
  };
  out.release_thread_tls = [](void *p, ThreadTLSAllocation *a) {
    return release_runtime_thread_tls(&static_cast<RuntimeState *>(p)->tls, a);
  };
  out.resolve_startup_tls = [](void *p, const TLSIndex *i) {
    return resolve_runtime_thread_tls(&static_cast<RuntimeState *>(p)->tls, i);
  };
  out.find_unwind_image = [](void *p, uintptr_t pc, UnwindImageView *v) {
    auto &state = *static_cast<RuntimeState *>(p);
    long error = find_runtime_unwind_image(&state.scopes, pc, v);
    // The interpreter is not part of the application's symbol namespace.
    return error == ENOENT && state.startup_unwind
               ? find_unwind_image(state.startup_unwind, pc, v)
               : error;
  };
  out.release_unwind_image = [](void *p, uintptr_t token) {
    auto &state = *static_cast<RuntimeState *>(p);
    if (state.startup_unwind && state.startup_unwind->owns(token))
      release_unwind_image(state.startup_unwind, token);
    else
      release_runtime_unwind_image(&state.scopes, token);
  };
  out.finalize_startup = [](void *p, LoaderCancellation hooks) {
    if (static_cast<RuntimeState *>(p)->api.finalize(hooks))
      fatal_startup();
  };
  out.run_initializers = [](void *p, LoaderCancellation hooks) {
    auto &state = *static_cast<RuntimeState *>(p);
    return state.api.run_startup(state.batch, hooks);
  };
  out.api = api.services();
  out.queries = api.query_services();
  out.process = api.process_services();
}
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
