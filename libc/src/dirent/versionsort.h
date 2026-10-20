//===-- Implementation header for versionsort -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_DIRENT_VERSIONSORT_H
#define LLVM_LIBC_SRC_DIRENT_VERSIONSORT_H
#include "hdr/types/struct_dirent.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
int versionsort(const struct dirent **left, const struct dirent **right);
}
#endif
