//===-- Definition of struct ipc_perm -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_LINUX_MMIX_STRUCT_IPC_PERM_H
#define LLVM_LIBC_TYPES_LINUX_MMIX_STRUCT_IPC_PERM_H

#include "llvm-libc-types/gid_t.h"
#include "llvm-libc-types/key_t.h"
#include "llvm-libc-types/mode_t.h"
#include "llvm-libc-types/uid_t.h"

#ifdef __linux__
struct ipc_perm {
  union {
    key_t __key;
    key_t key;
  };
  uid_t uid;
  gid_t gid;
  uid_t cuid;
  gid_t cgid;
  mode_t mode;
  unsigned short __seq;
  unsigned short __padding;
  unsigned long __unused_0;
  unsigned long __unused_1;
};
#else
#error "ipc_perm not defined for the target platform"
#endif

#endif // LLVM_LIBC_TYPES_LINUX_MMIX_STRUCT_IPC_PERM_H
