//===-- MMIX Linux cancellation windows --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CANCELLATION_POINT_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CANCELLATION_POINT_H

#include "src/__support/macros/config.h"
#include <stdint.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
struct ThreadControl;
constexpr int CANCEL_SIGNAL = 32;
constexpr uint64_t CANCEL_MASK = uint64_t(1) << (CANCEL_SIGNAL - 1);
void initialize_cancellation_windows(ThreadControl &control);
// Initial-thread startup only, before callbacks or publishing any worker.
// Installs notification handling without manufacturing an unwind root.
void initialize_cancellation_process();
bool cancellation_private_block();
int cancellation_public_mask(int how, const uint64_t *set, uint64_t *old);
long cancellation_syscall(unsigned long number, long a1 = 0, long a2 = 0,
                          long a3 = 0, long a4 = 0, long a5 = 0, long a6 = 0);
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
