//===-- Startup initializer scheduling ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_INITIALIZATION_H
#define LLVM_LIBC_LOADER_LINUX_INITIALIZATION_H

#include "loader/linux/entry.h"
#include "loader/linux/graph.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

class StartupInitializers {
  struct Object {
    const StartupGraph::Node *node;
    size_t first, count;
    size_t fini_first = 0, fini_count = 0;
  };
  StartupGraph *graph = nullptr;
  Object *order = nullptr;
  Address *callbacks = nullptr;
  size_t count = 0, preinit_count = 0, completed = 0;
  bool finalized = false;

public:
  // Borrow graph and arena for the full initialization/finalization lifetime.
  // Graph relocation and final protection must already have succeeded.
  // Snapshot and validate every callback before any user code can run.
  long prepare(StartupGraph &graph, Arena &arena);
  // The injectable invoker is an internal native-test boundary. Production
  // uses the ordinary MMIX function-pointer ABI with argc/argv/envp. Callbacks
  // must return normally. Shared libc/TLS adoption precedes run; escaping EH
  // and recursive loading are not admitted by this startup subset.
  using Invoker = void (*)(Address, int, char **, char **);
  long run(const InitialStack &stack, Invoker invoke = nullptr);
  using Finalizer = void (*)(Address);
  // Process termination owns serialization. Only fully initialized objects
  // participate; recursive/repeated calls cannot replay any callback.
  void finalize(Finalizer invoke = nullptr);
  size_t completed_count() const { return completed; }
  const StartupGraph::Node *completed_object(size_t i) const {
    return i < completed ? order[i].node : nullptr;
  }
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif // LLVM_LIBC_LOADER_LINUX_INITIALIZATION_H
