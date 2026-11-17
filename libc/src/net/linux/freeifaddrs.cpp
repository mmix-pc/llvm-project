//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/net/freeifaddrs.h"
#include "hdr/func/free.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(void, freeifaddrs, (ifaddrs * list)) {
  int saved_errno = libc_errno;
  while (list) {
    ifaddrs *next = list->ifa_next;
    ::free(list);
    list = next;
  }
  libc_errno = saved_errno;
}
} // namespace LIBC_NAMESPACE_DECL
