//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/resolv/ns_name_uncompress.h"
#include "src/__support/common.h"
#include "src/resolv/dns_utils.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, ns_name_uncompress,
                   (const unsigned char *msg, const unsigned char *end,
                    const unsigned char *src, char *dst, size_t size)) {
  if (!dst)
    return dns::fail();
  return dns::expand_name(msg, end, src, dst, size);
}

} // namespace LIBC_NAMESPACE_DECL
