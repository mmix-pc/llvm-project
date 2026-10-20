//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_RESOLV_NS_GET16_H
#define LLVM_LIBC_SRC_RESOLV_NS_GET16_H

#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

// The API has no end pointer: src must address at least two readable octets.
unsigned int ns_get16(const unsigned char *src);

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_RESOLV_NS_GET16_H
