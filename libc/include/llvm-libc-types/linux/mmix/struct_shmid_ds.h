//===-- struct_shmid_ds definitions ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_LINUX_MMIX_STRUCT_SHMID_DS_H
#define LLVM_LIBC_TYPES_LINUX_MMIX_STRUCT_SHMID_DS_H

#include "llvm-libc-types/struct_ipc_perm.h"
#include "llvm-libc-types/size_t.h"
#include "llvm-libc-types/time_t.h"
#include "llvm-libc-types/pid_t.h"
#include "llvm-libc-types/shmatt_t.h"

struct shmid_ds {
  struct ipc_perm shm_perm;
  size_t shm_segsz;
  time_t shm_atime;
  time_t shm_dtime;
  time_t shm_ctime;
  pid_t shm_cpid;
  pid_t shm_lpid;
  shmatt_t shm_nattch;
  unsigned long __unused4;
  unsigned long __unused5;
};

#endif // LLVM_LIBC_TYPES_LINUX_MMIX_STRUCT_SHMID_DS_H
