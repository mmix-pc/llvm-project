//===-- Implementation header for rewinddir ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_DIRENT_REWINDDIR_H
#define LLVM_LIBC_SRC_DIRENT_REWINDDIR_H
#include "hdr/types/DIR.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
void rewinddir(DIR *dir);
}
#endif
