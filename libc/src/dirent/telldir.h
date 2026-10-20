//===-- Implementation header for telldir -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_DIRENT_TELLDIR_H
#define LLVM_LIBC_SRC_DIRENT_TELLDIR_H
#include "hdr/types/DIR.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
long telldir(DIR *dir);
}
#endif
