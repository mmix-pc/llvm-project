//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_MNTENT_MNTENT_UTILS_H
#define LLVM_LIBC_SRC_MNTENT_MNTENT_UTILS_H
#include "hdr/types/struct_mntent.h"
#include "src/__support/File/file.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
using MountResize = void *(*)(void *, size_t);
struct mntent *read_mount_entry(File *stream, struct mntent *entry,
                                char *&buffer, size_t &capacity,
                                MountResize resize = nullptr);
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
