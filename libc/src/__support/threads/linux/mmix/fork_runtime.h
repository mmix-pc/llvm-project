//===-- MMIX Linux coordinated fork -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_FORK_RUNTIME_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_FORK_RUNTIME_H
#include "lifecycle.h"
namespace LIBC_NAMESPACE_DECL {
class File;
namespace mmix {
class ForkOperation {
  ThreadControl *caller = nullptr;
  uint64_t saved_mask = 0;
  bool serial = false, masked = false, suppressed = false;
  bool priority = false, streams = false, allocator = false, registry = false;
#ifdef LIBC_MMIX_SHARED_STARTUP
  bool loader_locked = false;
#endif
  int error_code = 0;
  File *standard[3]{};
  bool standard_locked[3]{};

public:
  ForkOperation();
  ~ForkOperation();
  ForkOperation(const ForkOperation &) = delete;
  ForkOperation &operator=(const ForkOperation &) = delete;
  int error() const { return error_code; }
  int prepare_runtime();
  int freeze_registry();
  void restore_runtime(bool child);
};
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
