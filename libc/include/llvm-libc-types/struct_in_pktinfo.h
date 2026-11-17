//===-- Definition of struct in_pktinfo
//--------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_STRUCT_IN_PKTINFO_H
#define LLVM_LIBC_TYPES_STRUCT_IN_PKTINFO_H

#include "struct_in_addr.h"

struct in_pktinfo {
  int ipi_ifindex;
  struct in_addr ipi_spec_dst;
  struct in_addr ipi_addr;
};

#endif // LLVM_LIBC_TYPES_STRUCT_IN_PKTINFO_H
