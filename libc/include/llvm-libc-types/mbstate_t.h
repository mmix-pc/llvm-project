//===-- Definition of mbstate_t type --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_MBSTATE_T_H
#define LLVM_LIBC_TYPES_MBSTATE_T_H

typedef struct {
  __UINT32_TYPE__ __field1;
  __UINT8_TYPE__ __field2;
  __UINT8_TYPE__ __field3;
} mbstate_t;

#endif // LLVM_LIBC_TYPES_MBSTATE_T_H
