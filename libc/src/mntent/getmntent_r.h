//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_MNTENT_GETMNTENT_R_H
#define LLVM_LIBC_SRC_MNTENT_GETMNTENT_R_H
#include "hdr/types/FILE.h"
#include "hdr/types/struct_mntent.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
struct mntent *getmntent_r(::FILE *stream, struct mntent *entry, char *buffer,
                           int size);
}
#endif
