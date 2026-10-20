//===-- Implementation header for scandirat ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_DIRENT_SCANDIRAT_H
#define LLVM_LIBC_SRC_DIRENT_SCANDIRAT_H
#include "hdr/types/struct_dirent.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
int scandirat(int fd, const char *path, struct dirent ***list,
              int (*filter)(const struct dirent *),
              int (*compare)(const struct dirent **, const struct dirent **));
}
#endif
