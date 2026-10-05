//===-- MMIX Linux cancellation-aware public mask -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include "src/signal/pthread_sigmask.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_sigmask,
                   (int how, const sigset_t *__restrict set,
                    sigset_t *__restrict oldset)) {
  return mmix::cancellation_public_mask(how, set ? &set->__signals[0] : nullptr,
                                        oldset ? &oldset->__signals[0]
                                               : nullptr);
}
} // namespace LIBC_NAMESPACE_DECL
