//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_NS_OPCODE_H
#define LLVM_LIBC_TYPES_NS_OPCODE_H

typedef enum __ns_opcode {
  ns_o_query = 0,
  ns_o_iquery = 1,
  ns_o_status = 2,
  ns_o_notify = 4,
  ns_o_update = 5,
  ns_o_max = 6
} ns_opcode;

#endif // LLVM_LIBC_TYPES_NS_OPCODE_H
