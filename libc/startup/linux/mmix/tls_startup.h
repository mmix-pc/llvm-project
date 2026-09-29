//===-- MMIX Linux prepared TLS startup state -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_STARTUP_LINUX_MMIX_TLS_STARTUP_H
#define LLVM_LIBC_STARTUP_LINUX_MMIX_TLS_STARTUP_H

#include "process_args.h"
#include "src/__support/threads/linux/mmix/main_thread.h"

#if LIBC_THREAD_MODE != LIBC_THREAD_MODE_PLATFORM
#error "MMIX Linux TLS startup requires platform TLS storage"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
struct StartupState {
  ProcessArgs args{};
  TLSDescriptor tls{};
  internal::MainThreadState thread;
};

// The returned storage and mapping live until process teardown. This function
// is TLS-free and must be compiled without protection or instrumentation.
StartupState *prepare_tls_startup(uintptr_t *stack);
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
