//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_STRUCT_NETENT_H
#define LLVM_LIBC_TYPES_STRUCT_NETENT_H

#include "../llvm-libc-macros/stdint-macros.h"

struct netent {
  char *n_name;
  char **n_aliases;
  int n_addrtype;
  uint32_t n_net;
};

#endif // LLVM_LIBC_TYPES_STRUCT_NETENT_H
