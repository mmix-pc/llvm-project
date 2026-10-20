//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_FTS_FTS_OPEN_H
#define LLVM_LIBC_SRC_FTS_FTS_OPEN_H
#include "hdr/types/FTS.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
FTS *fts_open(char *const *paths, int options, __fts_compare_t compare);
}
#endif
