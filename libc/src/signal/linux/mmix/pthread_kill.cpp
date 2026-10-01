//===-- MMIX Linux implementation of pthread_kill -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/signal/pthread_kill.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/thread_signal.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, pthread_kill, (pthread_t thread, int signal)) {
  return mmix::signal_thread(static_cast<ThreadAttributes *>(thread.__attrib),
                             signal);
}

} // namespace LIBC_NAMESPACE_DECL
