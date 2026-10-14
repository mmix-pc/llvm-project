//===-- Loader startup transaction -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_STARTUP_H
#define LLVM_LIBC_LOADER_LINUX_STARTUP_H

#include "loader/linux/initialization.h"
#include "loader/linux/unwind_images.h"
#include "loader/linux/runtime.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

// Process-lifetime state. Kernel mappings are borrowed; arena storage and the
// graph's DSO reservations are owned. No public libc or TLS services are used.
class Startup {
  Address page_size;
  Arena arena;
  ElfImage main;
  StartupGraph graph;
  StartupInitializers initializers;
  StartupUnwindImages unwind_images;
  StartupRuntime runtime;
  InitialStack stack;
  long cleanup_error = 0;
  bool started = false, prepared = false, running = false;
  bool callbacks_started = false;
  long prepare_impl(const InitialStack &input, const ElfImage &self);

public:
  explicit Startup(Address page) : page_size(page), arena(page), graph(page) {}
  Startup(const Startup &) = delete;
  Startup &operator=(const Startup &) = delete;
  // Kernel-provided pointers must describe readable initial mappings. This is
  // address/layout validation, not fault recovery from arbitrary host pointers.
  MapResult prepare(const InitialStack &input, const ElfImage &self);
  long run(StartupInitializers::Invoker invoke = nullptr);
  long prepare_runtime() {
    return runtime.prepare(graph, stack, unwind_images, initializers);
  }
  Address thread_pointer() const { return runtime.tp(); }
  long adopt_runtime() { return runtime.adopt(); }
  Address entry() const { return prepared ? stack.value(AuxField::Entry) : 0; }
  StartupUnwindImages *unwind_registry() {
    if (!prepared)
      return nullptr;
    // Exporting the service commits its images and metadata to process lifetime.
    if (!unwind_images.is_published() &&
        (callbacks_started || !unwind_images.publish()))
      return nullptr;
    return &unwind_images;
  }
  // Cleanup never releases main/interpreter/stack mappings. Failed releases
  // retain ownership for retry; a startup transaction cannot be reused.
  long reset();
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
