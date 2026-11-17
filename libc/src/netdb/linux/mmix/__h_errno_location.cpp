//===-- MMIX Linux network database provider ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/netdb/__h_errno_location.h"
#include "src/__support/common.h"
#include "src/netdb/linux/mmix/legacy_hosts.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int *, __h_errno_location, ()) {
  return &mmix::legacy_hosts::host_error;
}
} // namespace LIBC_NAMESPACE_DECL
