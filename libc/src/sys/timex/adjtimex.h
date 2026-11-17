// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIBC_SRC_SYS_TIMEX_ADJTIMEX_H
#define LLVM_LIBC_SRC_SYS_TIMEX_ADJTIMEX_H
#include "hdr/types/struct_timex.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
int adjtimex(struct timex *buffer);
}
#endif
