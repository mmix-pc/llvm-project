//===-- MMIX Linux semaphore status ------------------------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_STRUCT_SEMID_DS_H
#define LLVM_LIBC_TYPES_STRUCT_SEMID_DS_H

#include "llvm-libc-types/struct_ipc_perm.h"
#include "llvm-libc-types/time_t.h"

// MMIX uses the asm-generic 64-bit kernel layout without time-field padding.
struct semid_ds {
  struct ipc_perm sem_perm;
  time_t sem_otime;
  time_t sem_ctime;
  unsigned long sem_nsems;
  unsigned long __unused3;
  unsigned long __unused4;
};

#endif // LLVM_LIBC_TYPES_STRUCT_SEMID_DS_H
