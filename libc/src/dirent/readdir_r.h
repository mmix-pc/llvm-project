//===-- Implementation header for readdir_r ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_DIRENT_READDIR_R_H
#define LLVM_LIBC_SRC_DIRENT_READDIR_R_H
#include "hdr/types/DIR.h"
#include "hdr/types/struct_dirent.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
int readdir_r(DIR *__restrict dir, struct dirent *__restrict entry,
              struct dirent **__restrict result);
}
#endif
