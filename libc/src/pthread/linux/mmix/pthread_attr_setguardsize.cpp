//===-- MMIX Linux pthread guard-size attributes --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_attr_setguardsize.h"

#include "config/app.h"
#include "hdr/errno_macros.h"
#include "hdr/stdint_proxy.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

#include <pthread.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, pthread_attr_setguardsize,
                   (pthread_attr_t *__restrict attr, size_t guardsize)) {
  const size_t page = app.page_size;
  if (!page)
    return EINVAL;
  const size_t padding = (page - guardsize % page) % page;
  if (padding > SIZE_MAX - guardsize)
    return EINVAL;
  // Keep the request for getguardsize; creation rounds actual protection and
  // separately validates the combined owned-stack allocation.
  attr->__guardsize = guardsize;
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
