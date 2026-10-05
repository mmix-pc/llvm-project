//===-- MMIX Linux pthread_cancel implementation -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_cancel.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/cancellation.h"
#include "src/__support/threads/thread_attributes.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_cancel, (pthread_t thread)) {
  return mmix::cancel_thread(static_cast<ThreadAttributes *>(thread.__attrib));
}
} // namespace LIBC_NAMESPACE_DECL
