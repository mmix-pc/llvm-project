//===-- MMIX Linux network database provider ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC_NETDB_GETHOSTBYADDR_H
#define LLVM_LIBC_SRC_NETDB_GETHOSTBYADDR_H
#include "hdr/types/struct_hostent.h"
#include "hdr/types/socklen_t.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
hostent *gethostbyaddr(const void *address, socklen_t length, int family);
}
#endif
