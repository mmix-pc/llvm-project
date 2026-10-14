//===-- MMIX shared libc startup --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_STARTUP_LINUX_MMIX_SHARED_STARTUP_H
#define LLVM_LIBC_STARTUP_LINUX_MMIX_SHARED_STARTUP_H
#include "src/__support/OSUtil/linux/loader_services.h"
#include "src/__support/threads/linux/mmix/main_thread.h"
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
extern LIBC_CONSTINIT internal::MainThreadState shared_main;
long adopt_shared_main(const loader::StartupProcessView *,
                       const loader::LoaderServices *, void *,
                       const loader::ThreadTLSAllocation *);
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
