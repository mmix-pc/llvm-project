//===-- Definition of type __dirent_filter_t
//-------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES___DIRENT_FILTER_T_H
#define LLVM_LIBC_TYPES___DIRENT_FILTER_T_H
#include "struct_dirent.h"
typedef int (*__dirent_filter_t)(const struct dirent *);
#endif
