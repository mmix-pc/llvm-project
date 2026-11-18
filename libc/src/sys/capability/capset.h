// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIBC_SRC_SYS_CAPABILITY_CAPSET_H
#define LLVM_LIBC_SRC_SYS_CAPABILITY_CAPSET_H
#include "src/__support/macros/config.h"
struct __user_cap_header_struct;
struct __user_cap_data_struct;
namespace LIBC_NAMESPACE_DECL {
int capset(::__user_cap_header_struct *header, const ::__user_cap_data_struct *data);
}
#endif
