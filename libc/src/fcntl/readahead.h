// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIBC_SRC_FCNTL_READAHEAD_H
#define LLVM_LIBC_SRC_FCNTL_READAHEAD_H

#include "hdr/types/ssize_t.h"
#include "hdr/types/off_t.h"
#include "hdr/types/size_t.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
ssize_t readahead(int fd, off_t offset, size_t count);
} // namespace LIBC_NAMESPACE_DECL

#endif
