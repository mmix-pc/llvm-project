//===-- MMIX Linux scheduling attributes -------------------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_PTHREAD_ATTR_T_H
#define LLVM_LIBC_TYPES_PTHREAD_ATTR_T_H

#include "llvm-libc-types/size_t.h"

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

#endif
