//===-- Definition of type struct f_owner_ex ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_STRUCT_F_OWNER_EX_H
#define LLVM_LIBC_TYPES_STRUCT_F_OWNER_EX_H

#include "pid_t.h"

#if !defined(__LIBC_EXPLICIT_NAMESPACE) || defined(__LIBC_USE_GNU)
#define F_OWNER_TID 0
#endif
#define F_OWNER_PID 1
#define F_OWNER_PGRP 2

struct f_owner_ex {
  int type;
  pid_t pid;
};

#endif // LLVM_LIBC_TYPES_STRUCT_F_OWNER_EX_H
