//===-- MB_CUR_MAX implementation --------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC_LOCALE_MB_CUR_MAX_H
#define LLVM_LIBC_SRC_LOCALE_MB_CUR_MAX_H
#include "hdr/types/size_t.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
size_t __ctype_get_mb_cur_max();
}
#endif
