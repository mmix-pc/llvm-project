//===-- struct_shmid_ds definitions ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_STRUCT_SHMID_DS_H
#define LLVM_LIBC_TYPES_STRUCT_SHMID_DS_H

#if defined(__linux__) && defined(__mmix__)
#include "linux/mmix/struct_shmid_ds.h"
#else
#error "shmid_ds is not defined for this target"
#endif

#endif // LLVM_LIBC_TYPES_STRUCT_SHMID_DS_H
