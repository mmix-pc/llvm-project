//===-- Startup runtime handoff ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_RUNTIME_H
#define LLVM_LIBC_LOADER_LINUX_RUNTIME_H
#include "loader/linux/entry.h"
#include "loader/linux/graph.h"
#include "loader/linux/tls_resolver.h"
#include "loader/linux/unwind_images.h"
namespace LIBC_NAMESPACE_DECL {
namespace loader {
class StartupRuntime {
  TLSAllocationContext allocation_context{};
  StartupUnwindImages *images = nullptr;
  StartupProcessView process{};
  LoaderServices services{};
  LibcBootstrap bootstrap{};
  ThreadTLSAllocation allocation{};
  void *thread_state = nullptr;
  bool attempted = false, installed = false, active = false;

public:
  long prepare(const StartupGraph &, const InitialStack &,
               StartupUnwindImages &);
  long adopt();
  long reset();
  Address tp() const { return allocation.tp; }
  bool is_active() const { return active; }
};
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
