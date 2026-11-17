//===-- Definition of Dl_info_t type --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__mmix__) && defined(__linux__)
#include "linux/mmix/Dl_info_t.h"
#else
#ifndef LLVM_LIBC_TYPES_DL_INFO_T_H
#define LLVM_LIBC_TYPES_DL_INFO_T_H

#include "Dl_info.h"

typedef Dl_info Dl_info_t;

#endif // LLVM_LIBC_TYPES_DL_INFO_T_H
#endif // defined(__mmix__) && defined(__linux__)
