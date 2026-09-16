//===-- MMIX Linux kernel sigaction representation ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SIGNAL_LINUX_MMIX_KERNEL_SIGACTION_H
#define LLVM_LIBC_SRC_SIGNAL_LINUX_MMIX_KERNEL_SIGACTION_H

#include "hdr/signal_macros.h"
#include "hdr/types/struct_sigaction.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// Unlike the public type, the kernel puts the eight-byte mask last.
struct KernelSigaction {
  union {
    void (*sa_handler)(int);
    void (*sa_sigaction)(int, siginfo_t *, void *);
  };
  unsigned long sa_flags;
  void (*sa_restorer)(void);
  unsigned long sa_mask;
};

static_assert(sizeof(int) == 4 && sizeof(unsigned long) == 8);
static_assert(sizeof(sigset_t) == 8);
static_assert(sizeof(KernelSigaction) == 32 && alignof(KernelSigaction) == 8);
static_assert(__builtin_offsetof(KernelSigaction, sa_handler) == 0);
static_assert(__builtin_offsetof(KernelSigaction, sa_flags) == 8);
static_assert(__builtin_offsetof(KernelSigaction, sa_restorer) == 16);
static_assert(__builtin_offsetof(KernelSigaction, sa_mask) == 24);

LIBC_INLINE KernelSigaction to_kernel_sigaction(const struct sigaction &sa) {
  KernelSigaction kernel{};
  // SA_RESETHAND sets bit 31 of the public int; do not sign-extend it.
  kernel.sa_flags = static_cast<unsigned int>(sa.sa_flags);
  kernel.sa_restorer = sa.sa_restorer;
  kernel.sa_mask = sa.sa_mask.__signals[0];
  if (kernel.sa_flags & SA_SIGINFO)
    kernel.sa_sigaction = sa.sa_sigaction;
  else
    kernel.sa_handler = sa.sa_handler;
  return kernel;
}

LIBC_INLINE struct sigaction from_kernel_sigaction(const KernelSigaction &kernel) {
  struct sigaction sa{};
  sa.sa_flags = static_cast<int>(static_cast<unsigned int>(kernel.sa_flags));
  sa.sa_restorer = kernel.sa_restorer;
  sa.sa_mask.__signals[0] = kernel.sa_mask;
  if (kernel.sa_flags & SA_SIGINFO)
    sa.sa_sigaction = kernel.sa_sigaction;
  else
    sa.sa_handler = kernel.sa_handler;
  return sa;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_SIGNAL_LINUX_MMIX_KERNEL_SIGACTION_H
