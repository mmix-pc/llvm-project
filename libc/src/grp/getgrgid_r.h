//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_GRP_GETGRGID_R_H
#define LLVM_LIBC_SRC_GRP_GETGRGID_R_H
#include "hdr/types/size_t.h"
#include "hdr/types/struct_group.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
int getgrgid_r(gid_t gid, struct group *entry, char *buffer, size_t size,
               struct group **result);
}
#endif
