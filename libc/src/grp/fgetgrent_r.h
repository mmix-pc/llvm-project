//===-- Declaration of fgetgrent_r -------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_GRP_FGETGRENT_R_H
#define LLVM_LIBC_SRC_GRP_FGETGRENT_R_H
#include "hdr/types/FILE.h"
#include "hdr/types/size_t.h"
#include "hdr/types/struct_group.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
int fgetgrent_r(::FILE *, struct group *, char *, size_t, struct group **);
}
#endif
