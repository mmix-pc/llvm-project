//===-- MMIX Linux leased scheduling ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_SCHEDULING_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_THREAD_SCHEDULING_H
#include "lifecycle.h"
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
int schedule_thread(ThreadAttributes *handle, int &policy, int &priority,
                    bool write = false, bool priority_only = false);
}
} // namespace LIBC_NAMESPACE_DECL
#endif
