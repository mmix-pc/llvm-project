//===-- Definition of type posix_spawnattr_t ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_POSIX_SPAWNATTR_T_H
#define LLVM_LIBC_TYPES_POSIX_SPAWNATTR_T_H

#include "pid_t.h"
#include "sigset_t.h"

typedef struct {
  short __flags;
  pid_t __pgrp;
  int __priority;
  int __policy;
  sigset_t __sigdefault;
  sigset_t __sigmask;
} posix_spawnattr_t;

#endif // LLVM_LIBC_TYPES_POSIX_SPAWNATTR_T_H
