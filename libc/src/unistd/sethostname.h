// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIBC_SRC_UNISTD_SETHOSTNAME_H
#define LLVM_LIBC_SRC_UNISTD_SETHOSTNAME_H

#include "hdr/types/size_t.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
int sethostname(const char *name, size_t length);
} // namespace LIBC_NAMESPACE_DECL

#endif
