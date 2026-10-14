//===-- Startup TLS resolver ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/tls_resolver.h"
namespace LIBC_NAMESPACE_DECL {
namespace loader {
Address current_thread_pointer() {
  Address tp;
  asm volatile("OR %0, r230, 0" : "=r"(tp));
  return tp;
}
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
