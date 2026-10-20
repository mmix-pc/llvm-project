//===-- Internal FTS traversal engine ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_FTS_ENGINE_H
#define LLVM_LIBC_SRC_FTS_ENGINE_H
#include "include/llvm-libc-types/FTS.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace fts {
FTS *walk_open(char *const *, int, __fts_compare_t);
FTSENT *walk_read(FTS *);
FTSENT *walk_children(FTS *, int);
int walk_set(FTS *, FTSENT *, int);
int walk_close(FTS *);
} // namespace fts
} // namespace LIBC_NAMESPACE_DECL
#endif
