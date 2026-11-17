//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Implementation header for if_nameindex.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_NET_GETIFADDRS_H
#define LLVM_LIBC_SRC_NET_GETIFADDRS_H
#include "hdr/types/struct_ifaddrs.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
int getifaddrs(struct ifaddrs **list);
} // namespace LIBC_NAMESPACE_DECL
#endif
