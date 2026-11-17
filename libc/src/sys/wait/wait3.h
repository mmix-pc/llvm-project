// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIBC_SRC_SYS_WAIT_WAIT3_H
#define LLVM_LIBC_SRC_SYS_WAIT_WAIT3_H

#include "hdr/types/pid_t.h"
#include "hdr/types/struct_rusage.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
pid_t wait3(int *status, int options, struct rusage *usage);
} // namespace LIBC_NAMESPACE_DECL

#endif
