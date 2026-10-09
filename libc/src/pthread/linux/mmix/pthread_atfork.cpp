//===-- MMIX Linux coordinated atfork registration ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/pthread/pthread_atfork.h"
#include "hdr/errno_macros.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/atfork_callbacks.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_atfork,
                   (__atfork_callback_t prepare, __atfork_callback_t parent,
                    __atfork_callback_t child)) {
  return mmix::register_fork_callbacks(prepare, parent, child) ? 0 : ENOMEM;
}
} // namespace LIBC_NAMESPACE_DECL
