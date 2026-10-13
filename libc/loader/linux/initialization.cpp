//===-- Startup initializer scheduling ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/initialization.h"
#include "hdr/errno_macros.h"
#include "loader/linux/target.h"
#include "src/__support/CPP/new.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

static Address octa(const unsigned char *p) {
  Address value = 0;
  for (unsigned i = 0; i < 8; ++i)
    value = (value << 8) | p[i];
  return value;
}

static bool callable(const StartupGraph &graph, Address address) {
  if (!is_aligned_entry(address) || !is_user_range(address, 4))
    return false;
  for (auto *node = graph.scope(); node; node = node->next()) {
    const ElfImage &image = *node->image();
    Address runtime;
    if (address >= image.load_bias() &&
        image.runtime_address(address - image.load_bias(), 4, PF_R | PF_X,
                              runtime))
      return true;
  }
  return false;
}

long StartupInitializers::prepare(StartupGraph &input, Arena &arena) {
  if (graph || input.initializing || input.initialized)
    return -EBUSY;
  if (!input.protections_complete())
    return -EINVAL;
  const size_t n = input.size();
  struct State {
    const StartupGraph::Node *node;
    const StartupGraph::Edge *edge;
    size_t index = 0, low = 0, component = 0;
    bool active = false;
  };
  void *storage;
  if (n > __SIZE_MAX__ / sizeof(State) || n > __SIZE_MAX__ / sizeof(Object) ||
      n > __SIZE_MAX__ / (2 * sizeof(size_t)))
    return -EOVERFLOW;
  long error = arena.allocate(n * sizeof(State), alignof(State), storage);
  if (error)
    return error;
  auto *states = static_cast<State *>(storage);
  size_t i = 0;
  for (auto *node = input.scope(); node; node = node->next())
    new (states + i++) State{node, node->dependencies()};
  error = arena.allocate(2 * n * sizeof(size_t), alignof(size_t), storage);
  if (error)
    return error;
  auto *frames = static_cast<size_t *>(storage), *members = frames + n;
  size_t depth = 0, used = 0, serial = 0, components = 0;
  // Iterative Tarjan traversal avoids a host/guest C stack depth limit.
  // SCCs finish dependency-first. Main is always last, even for back-edges to
  // main; each dependency SCC is emitted in original BFS discovery order.
  for (size_t root = 1; root < n; ++root) {
    if (states[root].index)
      continue;
    frames[depth++] = root;
    while (depth) {
      size_t current = frames[depth - 1];
      State &state = states[current];
      if (!state.index) {
        state.index = state.low = ++serial;
        state.active = true;
        members[used++] = current;
      }
      if (state.edge) {
        auto *target = state.edge->target;
        state.edge = state.edge->next;
        size_t next = 0;
        while (next < n && states[next].node != target)
          ++next;
        if (next == n)
          return -ENOEXEC;
        if (!next)
          continue;
        if (!states[next].index)
          frames[depth++] = next;
        else if (states[next].active && states[next].index < state.low)
          state.low = states[next].index;
        continue;
      }
      if (state.low == state.index) {
        size_t member;
        do {
          member = members[--used];
          states[member].active = false;
          states[member].component = components;
        } while (member != current);
        ++components;
      }
      --depth;
      if (depth && state.low < states[frames[depth - 1]].low)
        states[frames[depth - 1]].low = state.low;
    }
  }
  error = arena.allocate(n * sizeof(Object), alignof(Object), storage);
  if (error)
    return error;
  auto *objects = static_cast<Object *>(storage);
  size_t ordered = 0;
  for (size_t component = 0; component < components; ++component)
    for (size_t j = 1; j < n; ++j)
      if (states[j].component == component)
        new (objects + ordered++) Object{states[j].node, 0, 0};
  new (objects + ordered++) Object{states[0].node, 0, 0};
  const auto &main = *states[0].node->metadata();
  size_t preinit = main.lifecycle_array(DynamicTag::PreinitArray).size() / 8;
  size_t total = preinit;
  for (size_t j = 0; j < n; ++j) {
    const auto &metadata = *objects[j].node->metadata();
    size_t number = metadata.lifecycle_array(DynamicTag::InitArray).size() / 8;
    if (metadata.value(DynamicTag::Init))
      ++number;
    objects[j].first = total;
    objects[j].count = number;
    if (__builtin_add_overflow(total, number, &total))
      return -EOVERFLOW;
  }
  if (total > __SIZE_MAX__ / sizeof(Address))
    return -EOVERFLOW;
  Address *calls = nullptr;
  if (total) {
    error = arena.allocate(total * sizeof(Address), alignof(Address), storage);
    if (error)
      return error;
    calls = static_cast<Address *>(storage);
  }
  size_t at = 0;
  auto copy_array = [&](Bytes bytes) {
    for (size_t offset = 0; offset < bytes.size(); offset += 8) {
      Address address = octa(bytes.data() + offset);
      // Unlike legacy .ctors, ELF init arrays contain callable entries, not
      // null/-1 sentinels. Cross-image relocated function pointers are valid.
      if (!callable(input, address))
        return false;
      calls[at++] = address;
    }
    return true;
  };
  if (!copy_array(main.lifecycle_array(DynamicTag::PreinitArray)))
    return -ENOEXEC;
  for (size_t j = 0; j < n; ++j) {
    const auto &object = *objects[j].node;
    const auto &metadata = *object.metadata();
    if (Address init = metadata.value(DynamicTag::Init)) {
      Address address;
      if (!object.image()->runtime_address(init, 4, PF_R | PF_X, address) ||
          !callable(input, address))
        return -ENOEXEC;
      calls[at++] = address;
    }
    if (!copy_array(metadata.lifecycle_array(DynamicTag::InitArray)))
      return -ENOEXEC;
  }
  graph = &input;
  order = objects;
  callbacks = calls;
  count = n;
  preinit_count = preinit;
  return 0;
}

static void invoke_callback(Address address, int argc, char **argv,
                            char **envp) {
  reinterpret_cast<void (*)(int, char **, char **)>(address)(argc, argv, envp);
}

long StartupInitializers::run(const InitialStack &stack, Invoker invoke) {
  if (!graph || !graph->protections_complete() || !stack.valid ||
      stack.argc > __INT_MAX__)
    return -EINVAL;
  if (graph->initializing)
    return -EBUSY;
  if (graph->initialized)
    return 0;
  graph->initializing = true;
  if (!invoke)
    invoke = invoke_callback;
  auto *argv = reinterpret_cast<char **>(stack.argv);
  auto *envp = reinterpret_cast<char **>(stack.envp);
  for (size_t j = 0; j < preinit_count; ++j)
    invoke(callbacks[j], static_cast<int>(stack.argc), argv, envp);
  for (size_t j = 0; j < count; ++j) {
    for (size_t k = 0; k < order[j].count; ++k)
      invoke(callbacks[order[j].first + k], static_cast<int>(stack.argc), argv,
             envp);
    ++completed;
  }
  graph->initialized = true;
  graph->initializing = false;
  return 0;
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
