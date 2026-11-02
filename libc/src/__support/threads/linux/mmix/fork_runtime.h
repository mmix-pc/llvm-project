//===-- MMIX Linux coordinated fork -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_FORK_RUNTIME_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_FORK_RUNTIME_H
#include "hdr/types/pid_t.h"
#include "lifecycle.h"
namespace LIBC_NAMESPACE_DECL {
class File;
namespace mmix {
class ForkOperation {
  ThreadControl *caller = nullptr;
  uint64_t saved_mask = 0;
  bool serial = false, masked = false, suppressed = false;
  uint64_t suppression_baseline = 0;
  bool priority = false, streams = false, allocator = false, registry = false;
  bool helper_drained = false;
  bool notifications = false;
  bool aio = false;
  bool minimal = false;
  ForkContinuation continuation = ForkContinuation::Ordinary;
  ForkSnapshot snapshot = ForkSnapshot::Recoverable;
  UnwindRoot continuation_root;
  bool credentials = false;
  bool exit_callbacks = false;
  bool message_bindings = false, message_results = false;
  bool random48_locked = false;
  bool random_locked = false;
  internal::ForkRecovery child_recovery = internal::ForkRecovery::child();
#ifdef LIBC_MMIX_SHARED_STARTUP
  bool loader_locked = false;
  bool resume_loader = false;
#endif
  int error_code = 0;
  pid_t previous_tid = 0;
  File *standard[3]{};
  File *stream_stop = nullptr;
  bool standard_locked[3]{};
  void select_continuation();

public:
  static bool active_on_this_thread();
  explicit ForkOperation(bool minimal = false);
  ~ForkOperation();
  ForkOperation(const ForkOperation &) = delete;
  ForkOperation &operator=(const ForkOperation &) = delete;
  int error() const { return error_code; }
  int prepare_runtime();
  int freeze_registry();
  long create_child(bool wait_for_exec = false);
  void restore_runtime(bool child);
};
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
