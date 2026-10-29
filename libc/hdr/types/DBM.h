//===-- Internal DBM type selection --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_HDR_TYPES_DBM_H
#define LLVM_LIBC_HDR_TYPES_DBM_H

#ifdef LIBC_FULL_BUILD
#include "include/llvm-libc-types/DBM.h"
#else
// Host ndbm headers may still use the incompatible pre-Issue-8 datum ABI.
#error "The Issue 8 DBM interface requires a full build"
#endif

#endif
