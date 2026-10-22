//===-- Definition of intptr_t type ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_INTPTR_T_H
#define LLVM_LIBC_TYPES_INTPTR_T_H

#if !defined(_INTPTR_T) && !defined(__intptr_t_defined)
typedef __INTPTR_TYPE__ intptr_t;
#define __intptr_t_defined
#define _INTPTR_T
#endif

#endif // LLVM_LIBC_TYPES_INTPTR_T_H
