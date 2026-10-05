//===-- At-fork callback helpers  -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_FORK_CALLBACKS_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_FORK_CALLBACKS_H

#include "src/__support/macros/config.h"
#include <stddef.h>

namespace LIBC_NAMESPACE_DECL {

using ForkCallback = void(void);

bool register_atfork_callbacks(ForkCallback *prepare_cd,
                               ForkCallback *parent_cb, ForkCallback *child_cb);
// Freeze this fork's callback boundary, run prepare callbacks, and lock the
// registry across fork. Exactly one post-fork invocation must release the lock,
// including when fork fails. Registrations during callbacks apply to later
// forks.
size_t invoke_prepare_callbacks();
void invoke_parent_callbacks(size_t count);
void invoke_child_callbacks(size_t count);

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_THREADS_FORK_CALLBACKS_H
