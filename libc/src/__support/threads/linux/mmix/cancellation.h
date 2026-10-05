//===-- MMIX Linux deferred cancellation -------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CANCELLATION_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CANCELLATION_H
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
struct ThreadAttributes;
namespace mmix {
int cancel_thread(ThreadAttributes *handle);
int set_cancel_state(int state, int *old_state);
int set_cancel_type(int type, int *old_type);
void test_cancel();
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
