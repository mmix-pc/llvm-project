//===-- MMIX Linux syscall interface -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_MMIX_SYSCALL_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_MMIX_SYSCALL_H

#if !defined(__mmix__) || !defined(__linux__)
#error "The MMIX Linux syscall adapter requires MMIX Linux"
#endif

// An ordinary external C call models register and syscall-visible memory
// effects. Do not mark this helper pure, const or otherwise memory-independent.
#ifdef __cplusplus
extern "C" {
#endif
__attribute__((visibility("hidden")))
long __llvm_libc_mmix_syscall(unsigned long number, long arg1, long arg2,
                             long arg3, long arg4, long arg5, long arg6);
#ifdef __cplusplus
}

#include "src/__support/common.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

LIBC_INLINE long syscall_impl(long number) {
  return __llvm_libc_mmix_syscall(static_cast<unsigned long>(number), 0, 0, 0, 0,
                                 0, 0);
}

LIBC_INLINE long syscall_impl(long number, long arg1) {
  return __llvm_libc_mmix_syscall(static_cast<unsigned long>(number), arg1, 0, 0,
                                 0, 0, 0);
}

LIBC_INLINE long syscall_impl(long number, long arg1, long arg2) {
  return __llvm_libc_mmix_syscall(static_cast<unsigned long>(number), arg1, arg2,
                                 0, 0, 0, 0);
}

LIBC_INLINE long syscall_impl(long number, long arg1, long arg2, long arg3) {
  return __llvm_libc_mmix_syscall(static_cast<unsigned long>(number), arg1, arg2,
                                 arg3, 0, 0, 0);
}

LIBC_INLINE long syscall_impl(long number, long arg1, long arg2, long arg3,
                              long arg4) {
  return __llvm_libc_mmix_syscall(static_cast<unsigned long>(number), arg1, arg2,
                                 arg3, arg4, 0, 0);
}

LIBC_INLINE long syscall_impl(long number, long arg1, long arg2, long arg3,
                              long arg4, long arg5) {
  return __llvm_libc_mmix_syscall(static_cast<unsigned long>(number), arg1, arg2,
                                 arg3, arg4, arg5, 0);
}

LIBC_INLINE long syscall_impl(long number, long arg1, long arg2, long arg3,
                              long arg4, long arg5, long arg6) {
  return __llvm_libc_mmix_syscall(static_cast<unsigned long>(number), arg1, arg2,
                                 arg3, arg4, arg5, arg6);
}

} // namespace LIBC_NAMESPACE_DECL
#endif

#endif
