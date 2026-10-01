//===-- MMIX Linux implementation of pthread_once -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_once.h"
#include "src/__support/common.h"
#include "src/__support/threads/callonce.h"
#include "src/__support/threads/linux/mmix/once.h"
#include <pthread.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, pthread_once,
                   (pthread_once_t * flag, void (*func)(void))) {
  auto *word = reinterpret_cast<CallOnceFlag *>(flag);
  return callonce(word, [=]() {
    // Unwind unlinks this record before the common guard resets and wakes.
    // Direct C exit instead drains the record at thread completion.
    mmix::OnceInitializer active(word);
    func();
  });
}
} // namespace LIBC_NAMESPACE_DECL
