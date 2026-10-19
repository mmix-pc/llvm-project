//===-- Loader cancellation admission tests -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/CPP/atomic.h"
#include "src/__support/macros/config.h"
#include <asm/rstack.h>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

// Exercise the production hooks with injected kernel and thread state, not
// native MMIX execution. Do not substitute the loader admission algorithm.
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_LIFECYCLE_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_MMIX_SYSCALL_H
#define LIBC_MMIX_SHARED_STARTUP
static uint64_t active_chain = 7, signal_mask;
static long query_failure;
static int cancel_state = PTHREAD_CANCEL_ENABLE;
static unsigned changes;
static void check(bool ok, int line = __builtin_LINE()) {
  if (!ok) {
    std::fprintf(stderr, "cancellation hook check at %d\n", line);
    std::abort();
  }
}
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
struct ThreadControl {
  uint64_t loader_chain = 7;
  cpp::Atomic<uint64_t> loader_scopes{0}, loader_callbacks{0};
};
static ThreadControl control;
static ThreadControl *current_control = &control;
int set_cancel_state(int state, int *previous) {
  check(state == PTHREAD_CANCEL_DISABLE || !control.loader_scopes.load());
  if (previous)
    *previous = cancel_state;
  cancel_state = state;
  ++changes;
  return 0;
}
} // namespace mmix
long syscall_impl(long number, long pointer) {
  check(number == __NR_mmix_rstack_query && signal_mask == UINT64_MAX);
  if (query_failure)
    return query_failure;
  reinterpret_cast<mmix_rstack_query *>(pointer)->chain_id = active_chain;
  return 0;
}
long syscall_impl(long number, long how, long set, long old, long size) {
  check(number == SYS_rt_sigprocmask && size == sizeof(uint64_t));
  if (old)
    *reinterpret_cast<uint64_t *>(old) = signal_mask;
  if (set) {
    uint64_t value = *reinterpret_cast<uint64_t *>(set);
    signal_mask = how == SIG_BLOCK ? signal_mask | value : value;
  }
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL

#include "src/__support/threads/linux/mmix/loader_cancellation.cpp"
#define __tls_get_addr mmix_test_tls_get_addr
#include "src/__support/OSUtil/linux/mmix/dynamic_tls.cpp"

namespace LIBC_NAMESPACE::mmix {
[[noreturn]] void fatal_dynamic_tls() { std::abort(); }
} // namespace LIBC_NAMESPACE::mmix

static unsigned fast_calls, slow_calls;
static int tls_value;
static void *fast_tls(void *context, const LIBC_NAMESPACE::loader::TLSIndex *i) {
  ++fast_calls;
  check(context == &tls_value);
  return i->module_id == 1 ? &tls_value : nullptr;
}
static void *slow_tls(void *context, const LIBC_NAMESPACE::loader::TLSIndex *) {
  ++slow_calls;
  check(cancel_state == PTHREAD_CANCEL_DISABLE);
  return context;
}

int main() {
  using namespace LIBC_NAMESPACE::mmix;
  auto hooks = loader_cancellation_hooks();
  uint64_t saved = 99, nested = 99;
  signal_mask = 0x40;
  check(!hooks.enter(&saved) && saved == PTHREAD_CANCEL_ENABLE);
  check(control.loader_scopes.load() == 1 && signal_mask == 0x40);
  hooks.begin_callback();
  check(control.loader_callbacks.load() == 1);
  check(!hooks.enter(&nested) && nested == PTHREAD_CANCEL_DISABLE);
  check(!hooks.leave(nested) && cancel_state == PTHREAD_CANCEL_DISABLE);
  hooks.end_callback();
  // A handler interrupting the owner cannot borrow its recursive lock rank.
  active_chain = 8;
  unsigned before = changes;
  check(hooks.enter(&nested) == -ENOTSUP && changes == before);
  check(control.loader_scopes.load() == 1 && signal_mask == 0x40);
  check(adopt_startup_tls_resolver({&tls_value, slow_tls, fast_tls}));
  LIBC_NAMESPACE::loader::TLSIndex index{1, 0};
  check(mmix_test_tls_get_addr(&index) == &tls_value);
  check(fast_calls == 1 && !slow_calls && changes == before);
  check(control.loader_scopes.load() == 1 && signal_mask == 0x40);
  // A miss in the handler must still fail closed, never allocate through the
  // suspended loader scope. Test the production wrapper and admission hook.
  pid_t child = fork();
  check(child >= 0);
  if (!child) {
    index.module_id = 2;
    (void)mmix_test_tls_get_addr(&index);
    _exit(1);
  }
  int status;
  check(waitpid(child, &status, 0) == child && WIFSIGNALED(status) &&
        WTERMSIG(status) == SIGABRT);
  active_chain = 7;
  query_failure = -EIO;
  check(hooks.enter(&nested) == -EIO && changes == before);
  query_failure = 0;
  check(!hooks.leave(saved) && cancel_state == PTHREAD_CANCEL_ENABLE);
  check(!control.loader_scopes.load() && !control.loader_callbacks.load());
  index.module_id = 2;
  check(mmix_test_tls_get_addr(&index) == &tls_value);
  check(fast_calls == 2 && slow_calls == 1);
  check(cancel_state == PTHREAD_CANCEL_ENABLE && !control.loader_scopes.load());
  check(signal_mask == 0x40);
  current_control = nullptr;
  check(hooks.enter(&saved) == -ENOTSUP && signal_mask == 0x40);
  current_control = &control;
  control.loader_chain = 0;
  check(hooks.enter(&saved) == -ENOTSUP && signal_mask == 0x40);
  return 0;
}
