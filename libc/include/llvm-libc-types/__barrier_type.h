//===-- Definition of __barrier_type type ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef __LLVM_LIBC_TYPES__BARRIER_TYPE_H
#define __LLVM_LIBC_TYPES__BARRIER_TYPE_H

#include <stdbool.h>

typedef struct __attribute__((aligned(8 /* alignof (Barrier) */))) {
  unsigned __expected;
  unsigned __waiting;
  bool __blocking;
  char __entering[24 /* sizeof (CndVar) */];
  char __exiting[24 /* sizeof (CndVar) */];
  char __mutex[24 /* sizeof (Mutex) */];
} __barrier_type;

#endif // __LLVM_LIBC_TYPES__BARRIER_TYPE_H
