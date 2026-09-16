//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_MMIX_UCONTEXT_T_H
#define LLVM_LIBC_TYPES_MMIX_UCONTEXT_T_H

#include "../sigset_t.h"
#include "../stack_t.h"
#include "mcontext_t.h"

typedef struct ucontext_t {
  unsigned long uc_flags;
  struct ucontext_t *uc_link;
  stack_t uc_stack;
  sigset_t uc_sigmask;
  // Linux reserves 128 bytes for the mask area before the machine context.
  unsigned char __unused[128 - sizeof(sigset_t)];
  mcontext_t uc_mcontext;
} ucontext_t;

#endif // LLVM_LIBC_TYPES_MMIX_UCONTEXT_T_H
