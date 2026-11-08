//===-- Definition of pthread_barrierattr_t type --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef __LLVM_LIBC_TYPES_PTHREAD_BARRIERATTR_T_H
#define __LLVM_LIBC_TYPES_PTHREAD_BARRIERATTR_T_H

typedef struct {
#ifdef __cplusplus
  bool __pshared;
#else
  _Bool __pshared;
#endif
} pthread_barrierattr_t;

#endif // __LLVM_LIBC_TYPES_PTHREAD_BARRIERATTR_T_H
