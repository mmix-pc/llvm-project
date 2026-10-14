//===-- MMIX Linux thread resource preparation ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_RESOURCES_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_RESOURCES_H

#include "src/__support/threads/linux/mmix/lifecycle.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// Only admitted internal attributes are represented here. Public wrappers must
// reject unsupported scheduling/affinity policies before preparing resources.
// Scheduling requests are applied before the child publishes READY.
struct ThreadPreparation {
  ThreadStyle style = ThreadStyle::POSIX;
  ThreadRunner runner{};
  void *argument = nullptr;
  void *stack = nullptr;
  size_t stack_size = 65536;
  size_t guard_size = 8192;
  bool detached = false;
  bool apply_scheduling = false;
  int scheduling_policy = 0, scheduling_priority = 0;
  uint64_t saved_signal_mask = 0;
  TLSImage image{};
  uintptr_t page_size = 8192;
};

// A standalone owner survives control unmapping, including partial rollback.
// Caller-supplied stack storage is never represented as an owned mapping.
struct ThreadResources {
  ThreadMapping control, stack;
  TLSDescriptor tls;
#ifdef LIBC_MMIX_SHARED_STARTUP
  loader::ThreadTLSAllocation shared_tls{};
#endif
  bool owns_mappings() const {
    bool owned = control.size || stack.size || tls.size;
#ifdef LIBC_MMIX_SHARED_STARTUP
    owned |= shared_tls.tls_mapping_size || shared_tls.dtv_mapping_size;
#endif
    return owned;
  }
};

enum class ThreadPrepareError {
  None,
  InvalidAttributes,
  Overflow,
  InvalidTLS,
  ControlMapFailed,
  StackMapFailed,
  InvalidMapping,
  ProtectFailed,
  TLSFailed
};

struct ThreadPreparationResult {
  ThreadPrepareError error;
  long syscall_error =
      0; // Positive errno, without changing the caller's errno.
  TLSError tls_error = TLSError::None;
  long rollback_error = 0;
  ThreadResources retained{};
};

// Success constructs and publishes PREPARED, but does not register, clone, set
// TP, alter signal state or run callbacks. Failure leaves output unchanged.
ThreadPreparationResult prepare_thread(const ThreadPreparation &request,
                                       ThreadControl *&output);

// Copy descriptors while the control is still pinned, before releasing it.
ThreadResources thread_resources(const ThreadControl &control);

// Release only after preclone failure or proven terminal/reference drain:
// no child/kernel/registry user may reference these resources. Each successful
// release clears its descriptor; failure retains it.
// Do not dereference the control after calling this, even on partial failure.
long release_thread_resources(ThreadResources &resources);

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
