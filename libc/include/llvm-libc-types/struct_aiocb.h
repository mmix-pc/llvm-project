//===-- Definition of struct aiocb -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_STRUCT_AIOCB_H
#define LLVM_LIBC_TYPES_STRUCT_AIOCB_H

#include "off_t.h"
#include "size_t.h"
#include "struct_sigevent.h"

struct __llvm_libc_aio_request;

struct aiocb {
  int aio_fildes;
  int aio_lio_opcode;
  int aio_reqprio;
  volatile void *aio_buf;
  size_t aio_nbytes;
  struct sigevent aio_sigevent;
  off_t aio_offset;
  struct __llvm_libc_aio_request *__request;
};

#endif // LLVM_LIBC_TYPES_STRUCT_AIOCB_H
