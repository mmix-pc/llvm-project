//===-- MMIX Linux configuration limits -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_UNISTD_LINUX_MMIX_SYSCONF_LIMITS_H
#define LLVM_LIBC_SRC_UNISTD_LINUX_MMIX_SYSCONF_LIMITS_H

#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

// Linux UAPI include/uapi/linux/mqueue.h fixes MQ_PRIO_MAX at 32768.
// This is a priority count, independent of namespace queue-capacity quotas.
constexpr long mq_priority_max() { return 32768; }

constexpr long tty_name_max() {
  // Both terminal-name providers use 4096-byte buffers, including NUL.
  // This fixed provider bound is independent of overlay host limits.h.
  return 4096;
}

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_UNISTD_LINUX_MMIX_SYSCONF_LIMITS_H
