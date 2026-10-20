//===-- Declaration of getgrouplist ------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_GRP_GETGROUPLIST_H
#define LLVM_LIBC_SRC_GRP_GETGROUPLIST_H
#include "hdr/types/gid_t.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
int getgrouplist(const char *, gid_t, gid_t *, int *);
}
#endif
