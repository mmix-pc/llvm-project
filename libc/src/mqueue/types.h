//===-- Message queue types ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_MQUEUE_TYPES_H
#define LLVM_LIBC_SRC_MQUEUE_TYPES_H

#ifdef LIBC_FULL_BUILD
#include "include/llvm-libc-types/mqd_t.h"
#include "include/llvm-libc-types/struct_mq_attr.h"
#else
#include <mqueue.h>
#endif

#endif // LLVM_LIBC_SRC_MQUEUE_TYPES_H
