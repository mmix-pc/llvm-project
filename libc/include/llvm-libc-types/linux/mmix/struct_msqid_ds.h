//===-- struct_msqid_ds definitions ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_LINUX_MMIX_STRUCT_MSQID_DS_H
#define LLVM_LIBC_TYPES_LINUX_MMIX_STRUCT_MSQID_DS_H

#include "llvm-libc-types/struct_ipc_perm.h"
#include "llvm-libc-types/time_t.h"
#include "llvm-libc-types/pid_t.h"
#include "llvm-libc-types/msglen_t.h"
#include "llvm-libc-types/msgqnum_t.h"

struct msqid_ds {
  struct ipc_perm msg_perm;
  time_t msg_stime;
  time_t msg_rtime;
  time_t msg_ctime;
  unsigned long __msg_cbytes;
  msgqnum_t msg_qnum;
  msglen_t msg_qbytes;
  pid_t msg_lspid;
  pid_t msg_lrpid;
  unsigned long __unused4;
  unsigned long __unused5;
};

#endif // LLVM_LIBC_TYPES_LINUX_MMIX_STRUCT_MSQID_DS_H
