//===-- MMIX Linux network database provider ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/netdb/gethostbyname.h"
#include "src/__support/common.h"
#include "src/netdb/linux/mmix/legacy_hosts.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(hostent *, gethostbyname, (const char *name)) {
  return mmix::legacy_hosts::forward(name);
}
} // namespace LIBC_NAMESPACE_DECL
