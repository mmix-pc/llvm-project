//===-- MMIX Linux private jump cleanup -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SETJMP_MMIX_LINUX_JUMP_CLEANUP_H
#define LLVM_LIBC_SRC_SETJMP_MMIX_LINUX_JUMP_CLEANUP_H

#include "jump_cleanup_stack.h"
#include "src/__support/macros/attributes.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// The assembly save bridge reads this pointer without creating a call window
// or invoking a dynamic TLS resolver. Publish/remove nodes only with signals
// blocked; the saved pointer denotes a live call, not a request/result owner.
extern LIBC_CONSTINIT LIBC_THREAD_LOCAL JumpCleanupStack::Node *
    jump_cleanup_head asm("__llvm_libc_mmix_jump_cleanup_head")
        __attribute__((tls_model("initial-exec"), visibility("hidden")));

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#endif
