//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_STRUCT_IFMAP_H
#define LLVM_LIBC_TYPES_STRUCT_IFMAP_H

#if !defined(_LINUX_IF_H) || !__UAPI_DEF_IF_IFMAP
struct ifmap {
  unsigned long mem_start;
  unsigned long mem_end;
  unsigned short base_addr;
  unsigned char irq;
  unsigned char dma;
  unsigned char port;
};
#undef __UAPI_DEF_IF_IFMAP
#define __UAPI_DEF_IF_IFMAP 0
#endif

#endif // LLVM_LIBC_TYPES_STRUCT_IFMAP_H
