// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LLVM_LIBC_TYPES_STRUCT_SOCKADDR_H
#define LLVM_LIBC_TYPES_STRUCT_SOCKADDR_H
#include <llvm-libc-types/sa_family_t.h>

// Linux UAPI embeds this complete object in ARP and routing requests.
struct __attribute__((may_alias)) sockaddr {
  sa_family_t sa_family;
  char sa_data[14];
};
#endif
