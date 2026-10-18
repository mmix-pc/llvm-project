//===-- Definition of char16_t--------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_HDR_TYPES_CHAR16_T_H
#define LLVM_LIBC_HDR_TYPES_CHAR16_T_H
#if defined(LIBC_FULL_BUILD) || defined(__APPLE__)
#include "include/llvm-libc-types/char16_t.h"
#else
#include "hdr/uchar_overlay.h"
#endif
#endif
