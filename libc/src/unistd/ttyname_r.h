//===-- Implementation header for ttyname_r ----------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC_UNISTD_TTYNAME_R_H
#define LLVM_LIBC_SRC_UNISTD_TTYNAME_R_H
#include "hdr/types/size_t.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
int ttyname_r(int fd, char *buffer, size_t size);
}
#endif
