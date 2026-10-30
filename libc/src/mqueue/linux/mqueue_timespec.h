//===-- Linux message queue timeout conversion -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_MQUEUE_LINUX_MQUEUE_TIMESPEC_H
#define LLVM_LIBC_SRC_MQUEUE_LINUX_MQUEUE_TIMESPEC_H

#include "hdr/stdint_proxy.h"
#include "hdr/types/struct_timespec.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace mqueue {

// Both native LP64 and the explicit time64 syscalls use two signed 64-bit
// words. Convert fields rather than passing a host timespec layout through.
static_assert(sizeof(long) == 8 && sizeof(void *) == 8,
              "message queue timeouts require the native LP64 Linux ABI");
static_assert(sizeof(timespec::tv_sec) == sizeof(int64_t));
static_assert(sizeof(timespec::tv_nsec) == sizeof(int64_t));

struct KernelTimespec {
  int64_t seconds;
  int64_t nanoseconds;
};
static_assert(sizeof(KernelTimespec) == 2 * sizeof(int64_t));

} // namespace mqueue
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_MQUEUE_LINUX_MQUEUE_TIMESPEC_H
