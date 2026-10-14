//===-- Startup TLS resolver ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/linux/mmix/dynamic_tls.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
static loader::StartupTLSResolver resolver{};
bool adopt_startup_tls_resolver(const loader::StartupTLSResolver &service) {
  if (resolver.resolve || !service.context || !service.resolve)
    return false;
  resolver.context = service.context;
  resolver.resolve = service.resolve;
  return true;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

extern "C" void *
__llvm_libc_tls_get_addr(const LIBC_NAMESPACE::loader::TLSIndex *index) {
  using namespace LIBC_NAMESPACE;
  if (!index || !mmix::resolver.resolve)
    mmix::fatal_dynamic_tls();
  void *result = mmix::resolver.resolve(mmix::resolver.context, index);
  if (!result)
    mmix::fatal_dynamic_tls();
  return result;
}
extern "C" __attribute__((alias("__llvm_libc_tls_get_addr"),
                          visibility("default"))) void *
__tls_get_addr(const LIBC_NAMESPACE::loader::TLSIndex *);
