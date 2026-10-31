//===-- MMIX Linux active once initializers --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_ONCE_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_ONCE_H

#include "src/__support/threads/linux/callonce.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

class OnceInitializer {
  CallOnceFlag *flag;
  OnceInitializer *previous;
  bool owns_completion;
  friend void abandon_once_initializers();
  friend void once_after_fork(bool);

public:
  // The non-coordinated wrapper borrows the generic guard's completion.
  explicit OnceInitializer(CallOnceFlag *flag, bool owns_completion = false);
  ~OnceInitializer();
  void complete();
  OnceInitializer(const OnceInitializer &) = delete;
  OnceInitializer &operator=(const OnceInitializer &) = delete;
};

// Retained direct C exit drains records before reclamation. In the forced-exit
// profile, stack unwinding already removed them and their guards reset them.
// Deferred cancellation uses the same forced-unwind abandonment ordering.
void abandon_once_initializers();

uint32_t once_fork_generation();
bool once_initializer_active();
void once_after_fork(bool quiescent = false);

// Keep completion compatible; tag only in-progress words in the fork profile.
template <class Callback>
int fork_aware_once(CallOnceFlag *flag, Callback callback) {
  constexpr uint32_t StateMask = 0xff;
  for (;;) {
    uint32_t status = flag->load(cpp::MemoryOrder::ACQUIRE);
    if (status == callonce_impl::FINISH)
      return 0;
    uint32_t generation = once_fork_generation();
    uint32_t start = generation | callonce_impl::START;
    uint32_t waiting = generation | callonce_impl::WAITING;
    if (!status || (status & ~StateMask) != generation) {
      if (flag->compare_exchange_strong(status, start)) {
        OnceInitializer active(flag, /*owns_completion=*/true);
        callback();
        active.complete();
        return 0;
      }
      continue;
    }
    if (status == start && !flag->compare_exchange_strong(status, waiting))
      continue;
    flag->wait(waiting);
  }
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
