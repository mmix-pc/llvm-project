//===-- MMIX Linux same-stack process creation ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_FORK_TRANSPORT_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_FORK_TRANSPORT_H

#include "hdr/types/pid_t.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
struct ForkResult {
  long value; // Child zero, parent PID, or negative errno.
  pid_t previous_tid;
};

// Caller masks application signals through its remaining recovery. Publish the new
// TID, but retain the old one for private lock rebinding. No callbacks, locks,
// allocation, robust-list changes or recovery-policy decisions occur here.
ForkResult fork_process();
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#endif
