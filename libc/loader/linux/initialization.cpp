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

long StartupInitializers::prepare(StartupGraph &input, Arena &arena) {
  if (graph || input.initializing || input.initialized)
    return -EBUSY;
  if (!input.protections_complete())
    return -EINVAL;
  long error = InitializationPlan::prepare(input, arena, true);
  if (!error)
    graph = &input;
  return error;
}

static void invoke_callback(Address address, int argc, char **argv,
                            char **envp) {
  reinterpret_cast<void (*)(int, char **, char **)>(address)(argc, argv, envp);
}

long StartupInitializers::run(const InitialStack &stack, Invoker invoke) {
  if (!graph || !graph->protections_complete() || !stack.valid ||
      stack.argc > __INT_MAX__)
    return -EINVAL;
  if (finalized)
    return -ECANCELED;
  if (graph->initializing)
    return -EBUSY;
  if (graph->initialized)
    return 0;
  graph->initializing = true;
  if (!invoke)
    invoke = invoke_callback;
  auto *argv = reinterpret_cast<char **>(stack.argv);
  auto *envp = reinterpret_cast<char **>(stack.envp);
  for (size_t j = 0; j < preinit_count; ++j) {
    invoke(callbacks[j], static_cast<int>(stack.argc), argv, envp);
    if (finalized) {
      graph->initializing = false;
      return -ECANCELED;
    }
  }
  for (size_t j = 0; j < count; ++j) {
    for (size_t k = 0; k < order[j].count; ++k) {
      invoke(callbacks[order[j].first + k], static_cast<int>(stack.argc), argv,
             envp);
      if (finalized) {
        graph->initializing = false;
        return -ECANCELED;
      }
    }
    ++completed;
  }
  graph->initialized = true;
  graph->initializing = false;
  return 0;
}

static void invoke_finalizer(Address address) {
  reinterpret_cast<void (*)()>(address)();
}

void StartupInitializers::finalize(Finalizer invoke) {
  if (!graph || finalized)
    return;
  finalized = true;
  if (!invoke)
    invoke = invoke_finalizer;
  for (size_t i = completed; i != 0; --i) {
    const Object &object = order[i - 1];
    for (size_t j = object.fini_count; j != 0; --j)
      invoke(callbacks[object.fini_first + j - 1]);
  }
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
