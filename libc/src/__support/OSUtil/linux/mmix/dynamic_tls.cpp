//===-- Startup TLS resolver ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/linux/mmix/dynamic_tls.h"
#ifdef LIBC_MMIX_SHARED_STARTUP
#include "src/__support/threads/linux/mmix/loader_cancellation.h"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
static loader::StartupTLSResolver resolver{};
bool adopt_startup_tls_resolver(const loader::StartupTLSResolver &service) {
  if (resolver.resolve || !service.context || !service.resolve)
    return false;
  resolver.context = service.context;
  resolver.try_resolve = service.try_resolve;
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
  // A signal may interrupt a loader transaction. Existing TLS needs neither
  // its locks nor cancellation-state changes; allocation still uses the guard.
  if (mmix::resolver.try_resolve)
    if (void *result =
            mmix::resolver.try_resolve(mmix::resolver.context, index))
      return result;
#ifdef LIBC_MMIX_SHARED_STARTUP
  auto hooks = mmix::loader_cancellation_hooks();
  uint64_t saved;
  if (hooks.enter(&saved))
    mmix::fatal_dynamic_tls();
#endif
  void *result = mmix::resolver.resolve(mmix::resolver.context, index);
  if (!result)
    mmix::fatal_dynamic_tls();
#ifdef LIBC_MMIX_SHARED_STARTUP
  if (hooks.leave(saved))
    mmix::fatal_dynamic_tls();
#endif
  return result;
}
extern "C" __attribute__((alias("__llvm_libc_tls_get_addr"),
                          visibility("default"))) void *
__tls_get_addr(const LIBC_NAMESPACE::loader::TLSIndex *);
