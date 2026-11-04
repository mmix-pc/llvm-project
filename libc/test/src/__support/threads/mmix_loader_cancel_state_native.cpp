//===-- Loader cancellation state regression ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Actual public-state and scope transitions with injected signal-mask/query
// transport. Target cancellation delivery is covered by the guest observer.
#include <cassert>
#include <cstdint>
#include <pthread.h>
#include <sys/syscall.h>
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_X86_64_SYSCALL_H
#define __mmix__ 1
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#undef __mmix__
#include "src/__support/threads/linux/mmix/cancellation.cpp"
#include "src/__support/threads/linux/mmix/loader_cancellation.cpp"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
LIBC_CONSTINIT LIBC_THREAD_LOCAL ThreadControl *current_control = nullptr;
}
}
namespace M = LIBC_NAMESPACE::mmix;
static uint64_t signal_mask;
static bool expect_disabled_on_unmask;
extern "C" long __llvm_libc_mmix_syscall(unsigned long number, long a1, long a2,
                                       long a3, long a4, long, long) {
  if (number == __NR_mmix_rstack_query) {
    reinterpret_cast<mmix_rstack_query *>(a1)->chain_id = 1;
    return 0;
  }
  assert(number == SYS_rt_sigprocmask && a4 == 8);
  if (a3) *reinterpret_cast<uint64_t *>(a3) = signal_mask;
  if (a2) {
    uint64_t next = *reinterpret_cast<const uint64_t *>(a2);
    signal_mask = a1 == SIG_BLOCK ? signal_mask | next : next;
  }
  if (expect_disabled_on_unmask && signal_mask != UINT64_MAX &&
      !M::current_control->loader_cancellation_blocked())
    assert(!M::current_control->cancellation.enabled());
  return 0;
}

int main() {
  M::ThreadControl control;
  M::current_control = &control;
  control.loader_chain = 1;
  auto hooks = M::loader_cancellation_hooks();
  const int states[] = {PTHREAD_CANCEL_DISABLE, PTHREAD_CANCEL_ENABLE};
  for (int initial : states) {
    assert(M::set_cancel_state(initial, nullptr) == 0);
    uint64_t outer, inner;
    assert(hooks.enter(&outer) == 0 && outer == uint64_t(initial));
    assert(control.loader_cancellation_blocked());
    int old = -1;
    assert(M::set_cancel_state(PTHREAD_CANCEL_ENABLE, &old) == 0);
    assert(old == PTHREAD_CANCEL_DISABLE && control.cancellation.enabled());
    assert(control.loader_cancellation_blocked());
    assert(hooks.enter(&inner) == 0 && inner == PTHREAD_CANCEL_ENABLE);
    assert(control.loader_scopes.load() == 2);
    assert(M::set_cancel_state(PTHREAD_CANCEL_ENABLE, &old) == 0);
    assert(old == PTHREAD_CANCEL_DISABLE);
    old = 123;
    assert(M::set_cancel_state(-123, &old) == EINVAL && old == 123);
    assert(control.cancellation.enabled());
    assert(hooks.leave(inner) == 0 && control.loader_scopes.load() == 1);
    assert(control.loader_cancellation_blocked() && control.cancellation.enabled());
    expect_disabled_on_unmask = initial == PTHREAD_CANCEL_DISABLE;
    assert(hooks.leave(outer) == 0);
    expect_disabled_on_unmask = false;
    assert(!control.loader_cancellation_blocked());
    assert(control.cancellation.enabled() == (initial == PTHREAD_CANCEL_ENABLE));
    assert(signal_mask == 0);
  }
}
