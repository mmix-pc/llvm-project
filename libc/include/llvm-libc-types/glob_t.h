//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_GLOB_T_H
#define LLVM_LIBC_TYPES_GLOB_T_H

#include "size_t.h"

typedef struct {
  size_t gl_pathc;
  char **gl_pathv;
  size_t gl_offs;
} glob_t;

#endif
