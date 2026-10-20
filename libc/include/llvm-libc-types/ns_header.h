//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_NS_HEADER_H
#define LLVM_LIBC_TYPES_NS_HEADER_H

// Integer fields retain network byte order. Flags occupy individual wire
// bytes, independent of the target's int width and alignment.
typedef struct {
  __UINT16_TYPE__ id;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  unsigned char qr : 1, opcode : 4, aa : 1, tc : 1, rd : 1;
  unsigned char ra : 1, unused : 1, ad : 1, cd : 1, rcode : 4;
#else
  unsigned char rd : 1, tc : 1, aa : 1, opcode : 4, qr : 1;
  unsigned char rcode : 4, cd : 1, ad : 1, unused : 1, ra : 1;
#endif
  __UINT16_TYPE__ qdcount, ancount, nscount, arcount;
} HEADER;

#endif // LLVM_LIBC_TYPES_NS_HEADER_H
