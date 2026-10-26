//===-- Implementation of bindtextdomain --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/libintl/bindtextdomain.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/libintl/domain_bindings.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(char *, bindtextdomain,
                   (const char *domain, const char *value)) {
  int saved_errno = libc_errno;
  auto result = internal::bind_text_domain(domain, value,
                                           internal::DomainBinding::DIRECTORY);
  if (!result) {
    libc_errno = result.error();
    return nullptr;
  }
  libc_errno = saved_errno;
  return *result;
}
} // namespace LIBC_NAMESPACE_DECL
