//===-- MMIX Linux network database provider ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/netdb/gethostbyaddr.h"
#include "src/__support/common.h"
#include "src/netdb/linux/mmix/legacy_hosts.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(hostent *, gethostbyaddr,
                   (const void *address, socklen_t length, int family)) {
  return mmix::legacy_hosts::reverse(address, length, family);
}
} // namespace LIBC_NAMESPACE_DECL
