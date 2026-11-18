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
#ifdef LIBC_MMIX_SHARED_STARTUP
#include <asm/rstack.h>
#include <asm/unistd.h>
#endif

namespace LIBC_NAMESPACE_DECL {
// The C-only companion does not provide these barriers. Full shared libc++abi
// provides them before any constructors run; never add a bootstrap dependency.
extern "C" [[gnu::weak]] void
__cxa_mmix_invoke_initializer(uintptr_t, int, char **, char **);
extern "C" [[gnu::weak]] void __cxa_mmix_invoke_finalizer(uintptr_t);
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
#ifdef LIBC_MMIX_SHARED_STARTUP
  // A handler must not reenter a suspended loader transaction. Signal image
  // leases use their separate lock-free service and never enter this scope.
  mmix_rstack_query query{};
  if (!current_control || !current_control->loader_chain)
    return -ENOTSUP;
  long query_error = syscall_impl(__NR_mmix_rstack_query,
                                 reinterpret_cast<long>(&query));
  if (query_error)
    return query_error;
  if (query.chain_id != current_control->loader_chain)
    return -ENOTSUP;
#endif
  int previous;
#ifdef LIBC_MMIX_SHARED_STARTUP
  set_cancel_state_masked(PTHREAD_CANCEL_DISABLE, &previous);
  *saved = static_cast<uint64_t>(previous);
  if (current_control->loader_scopes.fetch_add(1) == UINT64_MAX)
    __builtin_trap();
  return 0;
#else
  int error = set_cancel_state(PTHREAD_CANCEL_DISABLE, &previous);
  if (!error)
    *saved = static_cast<uint64_t>(previous);
  return -error;
#endif
}
long leave(uint64_t saved) {
  if (saved != PTHREAD_CANCEL_ENABLE && saved != PTHREAD_CANCEL_DISABLE)
    return -EINVAL;
  int state = static_cast<int>(saved);
#ifdef LIBC_MMIX_SHARED_STARTUP
  {
    Mask mask;
    if (!current_control || !current_control->loader_scopes.fetch_sub(1))
      __builtin_trap();
    // Restore state and queue any wake before unmasking. Remaining outer
    // scopes still block delivery even if a callback enabled cancellation.
    set_cancel_state_masked(state, nullptr);
  }
  finish_cancel_state_change(state);
  return 0;
#else
  return -set_cancel_state(state, nullptr);
#endif
}
#ifdef LIBC_MMIX_SHARED_STARTUP
void change_callback_depth(bool entering) {
  auto *control = current_control;
  if (!control || !control->loader_scopes.load())
    __builtin_trap();
  // The owning loader scope stays active across both transitions. Readers
  // can observe either depth, and no callback runs until entry publishes
  // the new depth. Check the bounds before storing to avoid publishing a
  // wrapped depth to a signal handler, even on a fatal path.
  auto &depth = control->loader_callbacks;
  uint64_t previous = depth.load();
  do {
    if (entering ? previous == UINT64_MAX : previous == 0)
      __builtin_trap();
  } while (!depth.compare_exchange_weak(
      previous, entering ? previous + 1 : previous - 1));
}
#endif
void begin_callback() {
#ifdef LIBC_MMIX_SHARED_STARTUP
  change_callback_depth(true);
#endif
}
void end_callback() {
#ifdef LIBC_MMIX_SHARED_STARTUP
  change_callback_depth(false);
#endif
}
} // namespace
loader::LoaderCancellation loader_cancellation_hooks() {
  return {enter, leave, begin_callback, end_callback,
          __cxa_mmix_invoke_initializer, __cxa_mmix_invoke_finalizer};
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
