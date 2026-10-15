//===-- Loader cancellation scope -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader_cancellation.h"
#include "cancellation.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "lifecycle.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include <pthread.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {
class Mask {
  uint64_t saved = 0;

public:
  Mask() {
    uint64_t full = UINT64_MAX;
    if (syscall_impl(SYS_rt_sigprocmask, SIG_BLOCK,
                     reinterpret_cast<long>(&full),
                     reinterpret_cast<long>(&saved), sizeof(saved)))
      __builtin_trap();
  }
  ~Mask() {
    if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                     reinterpret_cast<long>(&saved), 0, sizeof(saved)))
      __builtin_trap();
  }
};
long enter(uint64_t *saved) {
  Mask mask;
  int previous;
  int error = set_cancel_state(PTHREAD_CANCEL_DISABLE, &previous);
  if (!error) {
    *saved = static_cast<uint64_t>(previous);
#ifdef LIBC_MMIX_SHARED_STARTUP
    if (current_control->loader_scopes.fetch_add(1) == UINT64_MAX)
      __builtin_trap();
#endif
  }
  return -error;
}
long leave(uint64_t saved) {
  if (saved != PTHREAD_CANCEL_ENABLE && saved != PTHREAD_CANCEL_DISABLE)
    return -EINVAL;
#ifdef LIBC_MMIX_SHARED_STARTUP
  {
    Mask mask;
    if (!current_control || !current_control->loader_scopes.fetch_sub(1))
      __builtin_trap();
  }
#endif
  // Unlike notification masking alone, the disabled state also suppresses
  // explicit testcancel/cancellation leaves inside loader callbacks.
  return -set_cancel_state(static_cast<int>(saved), nullptr);
}
void begin_callback() {
#ifdef LIBC_MMIX_SHARED_STARTUP
  Mask mask;
  if (!current_control || !current_control->loader_scopes.load() ||
      current_control->loader_callbacks.fetch_add(1) == UINT64_MAX)
    __builtin_trap();
#endif
}
void end_callback() {
#ifdef LIBC_MMIX_SHARED_STARTUP
  Mask mask;
  if (!current_control || !current_control->loader_callbacks.fetch_sub(1))
    __builtin_trap();
#endif
}
} // namespace
loader::LoaderCancellation loader_cancellation_hooks() {
  return {enter, leave, begin_callback, end_callback};
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
