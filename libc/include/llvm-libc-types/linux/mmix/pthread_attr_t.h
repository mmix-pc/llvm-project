//===-- MMIX Linux scheduling attributes -------------------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_PTHREAD_ATTR_T_H
#define LLVM_LIBC_TYPES_PTHREAD_ATTR_T_H

#include "size_t.h"

typedef struct {
  int __detachstate;
  void *__stack;
  size_t __stacksize;
  size_t __guardsize;
  int __schedpolicy;
  int __schedpriority;
  int __inheritsched;
  int __scope;
} pthread_attr_t;

#define PTHREAD_INHERIT_SCHED 0
#define PTHREAD_EXPLICIT_SCHED 1
#define PTHREAD_SCOPE_SYSTEM 0
#define PTHREAD_SCOPE_PROCESS 1
#define __LLVM_LIBC_MMIX_PTHREAD_ATTR_ABI 2

// Attribute consumers must not link against the incompatible smaller layout.
#define pthread_create __llvm_libc_mmix_pthread_create_v2
#define pthread_attr_init __llvm_libc_mmix_pthread_attr_init_v2
#define pthread_attr_destroy __llvm_libc_mmix_pthread_attr_destroy_v2
#define pthread_attr_getdetachstate                                            \
  __llvm_libc_mmix_pthread_attr_getdetachstate_v2
#define pthread_attr_setdetachstate                                            \
  __llvm_libc_mmix_pthread_attr_setdetachstate_v2
#define pthread_attr_getguardsize __llvm_libc_mmix_pthread_attr_getguardsize_v2
#define pthread_attr_setguardsize __llvm_libc_mmix_pthread_attr_setguardsize_v2
#define pthread_attr_getstack __llvm_libc_mmix_pthread_attr_getstack_v2
#define pthread_attr_setstack __llvm_libc_mmix_pthread_attr_setstack_v2
#define pthread_attr_getstacksize __llvm_libc_mmix_pthread_attr_getstacksize_v2
#define pthread_attr_setstacksize __llvm_libc_mmix_pthread_attr_setstacksize_v2
#define pthread_attr_getschedparam                                             \
  __llvm_libc_mmix_pthread_attr_getschedparam_v2
#define pthread_attr_setschedparam                                             \
  __llvm_libc_mmix_pthread_attr_setschedparam_v2
#define pthread_attr_getschedpolicy                                            \
  __llvm_libc_mmix_pthread_attr_getschedpolicy_v2
#define pthread_attr_setschedpolicy                                            \
  __llvm_libc_mmix_pthread_attr_setschedpolicy_v2
#define pthread_attr_getinheritsched                                           \
  __llvm_libc_mmix_pthread_attr_getinheritsched_v2
#define pthread_attr_setinheritsched                                           \
  __llvm_libc_mmix_pthread_attr_setinheritsched_v2
#define pthread_attr_getscope __llvm_libc_mmix_pthread_attr_getscope_v2
#define pthread_attr_setscope __llvm_libc_mmix_pthread_attr_setscope_v2
#define pthread_getattr_np __llvm_libc_mmix_pthread_getattr_np_v2

#endif
