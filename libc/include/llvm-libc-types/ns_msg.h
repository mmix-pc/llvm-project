//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_NS_MSG_H
#define LLVM_LIBC_TYPES_NS_MSG_H

#include "ns_sect.h"

typedef struct __ns_msg {
  const unsigned char *_msg, *_eom;
  __UINT16_TYPE__ _id, _flags, _counts[ns_s_max];
  const unsigned char *_sections[ns_s_max];
  ns_sect _sect;
  int _rrnum;
  const unsigned char *_msg_ptr;
} ns_msg;

#endif // LLVM_LIBC_TYPES_NS_MSG_H
