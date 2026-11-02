//===-- Native MMIX nonblocking signal cleanup claim ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Production registry admission with injected futex transport. This does not
// exercise real signal entry, target unwinding or public cancellation policy.
#include <atomic>
#include <cassert>
#include <cstring>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>

#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_X86_64_SYSCALL_H
#define __mmix__ 1
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#undef __mmix__
#include "src/__support/threads/linux/mmix/lifecycle.cpp"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
LIBC_CONSTINIT LIBC_THREAD_LOCAL ThreadControl *current_control = nullptr;
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

namespace M = LIBC_NAMESPACE::mmix;

extern "C" long __llvm_libc_mmix_syscall(unsigned long number, long, long op,
                                         long, long, long, long) {
  // A blocking-lock regression must fail here, not wait for the held lock.
  assert(number == SYS_futex && op == FUTEX_WAKE_PRIVATE);
  return 0;
}

struct Environment {
  M::ThreadRegistry registry;
  M::ThreadControl control;

  Environment() {
    assert(!M::current_control);
    control.attributes.style = LIBC_NAMESPACE::ThreadStyle::STDC;
    M::ThreadRegistryLock lock(registry);
    assert(lock.reserve() && lock.insert(control, false));
    assert(lock.start(control) && lock.drop_creator(control));
    M::current_control = &control;
  }

  ~Environment() { M::current_control = nullptr; }

  void request() {
    M::ThreadRegistryLock lock(registry);
    assert(lock.request_cancellation(control, true) == 0);
  }
};

static void rejected(M::ThreadRegistry &registry, M::ThreadControl &control) {
  // Include private execution state and pending bits in the no-mutation check.
  unsigned char before[sizeof(control)];
  std::memcpy(before, &control, sizeof(control));
  assert(!registry.try_signal_cleanup(control));
  assert(std::memcmp(before, &control, sizeof(control)) == 0);
}

static void claim_once(Environment &env) {
  auto &control = env.control;
  assert(control.cancellation.ready() && !control.cancellation.terminal());
  assert(env.registry.try_signal_cleanup(control));
  assert(control.cancellation.terminal() && !control.cancellation.ready());
  assert(control.worker_cleanup_pending()); // Running became Cleaning.
  rejected(env.registry, control);
  {
    M::ThreadRegistryLock lock(env.registry);
    assert(!lock.begin_cleanup(control, true));
    assert(!lock.begin_cleanup(control));
    assert(lock.live_threads() == 1);
    assert(lock.close_leases(control) && lock.exit_ready(control, 73));
    assert(lock.live_threads() == 0);
  }
  assert(!control.worker_cleanup_pending());
  assert(control.attributes.retval.stdc_retval == 73);
  rejected(env.registry, control); // ExitReady cannot reenter cleanup.
}

static void no_pending() {
  Environment env;
  assert(!env.control.cancellation.ready());
  rejected(env.registry, env.control);
  assert(!env.control.cancellation.terminal());
  env.request();
  claim_once(env);
}

static void disabled_pending() {
  Environment env;
  assert(env.control.cancellation.set_enabled(false));
  env.request();
  rejected(env.registry, env.control);
  assert(!env.control.cancellation.enabled());
  assert(!env.control.cancellation.terminal());
  assert(!env.control.cancellation.set_enabled(true));
  // No second request: the disabled rejection must preserve pending.
  claim_once(env);
}

static void external_owner() {
  Environment env;
  M::ThreadControl other;
  env.request();
  M::current_control = nullptr;
  rejected(env.registry, env.control);
  M::current_control = &other;
  rejected(env.registry, env.control);
  M::current_control = &env.control;
  claim_once(env);
}

static void foreign_registry() {
  Environment env;
  M::ThreadRegistry other;
  env.request();
  rejected(other, env.control);
  // A failed admission must also release the foreign registry's mutex.
  {
    M::ThreadRegistryLock lock(other);
    assert(lock.live_threads() == 0);
  }
  claim_once(env);
}

static void held_current_scope() {
  Environment env;
  env.request();
  {
    M::ThreadRegistryLock lock(env.registry);
    rejected(env.registry, env.control);
    assert(env.control.cancellation.ready());
    assert(lock.live_threads() == 1);
    // Rejection must not accidentally unlock the interrupted holder's mutex.
    rejected(env.registry, env.control);
  }
  claim_once(env);
}

struct HeldLock {
  M::ThreadRegistry &registry;
  std::atomic<bool> held{false}, release{false};
};

static void *hold_registry(void *argument) {
  auto &state = *static_cast<HeldLock *>(argument);
  assert(!M::current_control);
  M::ThreadRegistryLock lock(state.registry);
  state.held.store(true, std::memory_order_release);
  while (!state.release.load(std::memory_order_acquire))
    sched_yield();
  return nullptr;
}

static void held_other_thread() {
  Environment env;
  env.request();
  HeldLock state{env.registry};
  pthread_t holder;
  assert(pthread_create(&holder, nullptr, hold_registry, &state) == 0);
  while (!state.held.load(std::memory_order_acquire))
    sched_yield();
  // The holder cannot release until both nonblocking attempts return.
  rejected(env.registry, env.control);
  assert(env.control.cancellation.ready());
  rejected(env.registry, env.control);
  state.release.store(true, std::memory_order_release);
  assert(pthread_join(holder, nullptr) == 0);
  claim_once(env);
}

static void normal_cleanup() {
  Environment env;
  env.request();
  {
    M::ThreadRegistryLock lock(env.registry);
    assert(lock.begin_cleanup(env.control));
  }
  assert(env.control.cancellation.terminal());
  rejected(env.registry, env.control);
  {
    M::ThreadRegistryLock lock(env.registry);
    assert(lock.close_leases(env.control));
    assert(lock.exit_ready(env.control, 91));
  }
  rejected(env.registry, env.control);
  assert(env.control.attributes.retval.stdc_retval == 91);
}

static void terminal_running() {
  Environment env;
  env.request();
  // Isolate the cancellation terminal guard while execution is still Running.
  assert(env.control.cancellation.claim(false));
  rejected(env.registry, env.control);
  assert(env.control.cancellation.terminal());
  assert(!env.control.worker_cleanup_pending());
}

int main() {
  no_pending();
  disabled_pending();
  external_owner();
  foreign_registry();
  held_current_scope();
  held_other_thread();
  normal_cleanup();
  terminal_running();
  return 0;
}
