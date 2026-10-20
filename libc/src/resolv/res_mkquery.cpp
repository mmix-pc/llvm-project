//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/resolv/res_mkquery.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/getrandom.h"
#include "src/__support/common.h"
#include "src/resolv/dns_utils.h"
#include "src/resolv/query.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, res_mkquery,
                   (int op, const char *name, int rr_class, int type,
                    const unsigned char *data, int datalen,
                    const unsigned char *newrr, unsigned char *buffer,
                    int buflen)) {
  return dns::make_query(op, name, rr_class, type, data, datalen, newrr, buffer,
                         buflen, linux_syscalls::getrandom);
}

} // namespace LIBC_NAMESPACE_DECL
