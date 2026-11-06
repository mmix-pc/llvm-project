//===-- Native MMIX completed-thread cancellation regression --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Exercise the production pthread_cancel backend and registry. Syscall
// interception observes notification attempts, not target signal delivery.
#include <atomic>
#include <cassert>
#include <cerrno>
#include <condition_variable>
#include <initializer_list>
#include <linux/futex.h>
#include <mutex>
#include <pthread.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_X86_64_SYSCALL_H
#define __mmix__ 1
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#undef __mmix__
#include "src/__support/threads/linux/mmix/lifecycle.cpp"
#include "src/__support/threads/linux/mmix/cancellation.cpp"

// Keep host std::thread's pthread ABI separate from the target public handle.
#define pthread_t MMIXPthread
#include "include/llvm-libc-types/pthread_t.h"
#include "src/pthread/linux/mmix/pthread_cancel.cpp"
#undef pthread_t

namespace LIBC_NAMESPACE_DECL::mmix {
LIBC_CONSTINIT LIBC_THREAD_LOCAL ThreadControl *current_control = nullptr;
}
namespace M = LIBC_NAMESPACE::mmix;
using M::ThreadResultKind;
using LIBC_NAMESPACE::ThreadReturnValue;

struct Gate {
  std::mutex mutex;
  std::condition_variable changed;
  bool arrived = false, released = false;
  void pause() {
    std::unique_lock<std::mutex> lock(mutex);
    arrived = true;
    changed.notify_all();
    changed.wait(lock, [&] { return released; });
  }
  void wait() {
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait(lock, [&] { return arrived; });
  }
  void release() {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    changed.notify_all();
  }
};

static thread_local uint64_t signal_mask = 0x1234;
static thread_local Gate *notification_gate, *return_gate;
static std::atomic<unsigned> pid_calls{0}, signal_calls{0};
static thread_local long signal_result;
static constexpr int TargetTID = 7654;

extern "C" long __llvm_libc_mmix_syscall(unsigned long number, long a1, long a2,
                                       long a3, long a4, long a5, long a6) {
  if (number == SYS_rt_sigprocmask) {
    assert(a1 == SIG_SETMASK && a4 == 8);
    if (a3)
      *reinterpret_cast<uint64_t *>(a3) = signal_mask;
    signal_mask = *reinterpret_cast<const uint64_t *>(a2);
    if (signal_mask != UINT64_MAX && return_gate)
      return_gate->pause();
    return 0;
  }
  if (number == SYS_getpid) {
    ++pid_calls;
    return 123;
  }
  if (number == SYS_tgkill) {
    assert(a1 == 123 && a2 == TargetTID && a3 == 32);
    ++signal_calls;
    if (notification_gate)
      notification_gate->pause();
    return signal_result;
  }
  assert(number == SYS_futex);
  int saved = errno;
  long result = syscall(number, a1, a2, a3, a4, a5, a6);
  if (result == -1)
    result = -errno;
  errno = saved;
  return result;
}

static void publish(M::ThreadControl &c, bool detached = false,
                    bool helper = false, bool start = true) {
  c.attributes.tid = TargetTID;
  M::ThreadRegistryLock lock(M::thread_registry);
  assert(helper ? lock.begin_helper_creation() : lock.reserve());
  assert(lock.insert(c, detached, helper));
  if (start)
    assert(lock.start(c));
  assert(lock.drop_creator(c));
}

static void complete(M::ThreadControl &c, void *result) {
  M::ThreadRegistryLock lock(M::thread_registry);
  assert(lock.begin_cleanup(c));
  assert(lock.close_leases(c));
  assert(lock.exit_ready(c, ThreadReturnValue(result)));
}

static void reap(M::ThreadControl &c) {
  M::ThreadRegistryLock lock(M::thread_registry);
  c.clear_tid.value.store(0);
  if (lock.owner(c) == M::ThreadOwner::Joinable) {
    assert(lock.pin(&c.attributes) == &c && lock.claim_join(c));
    assert(lock.unpin(c));
  }
  assert(lock.begin_reaping(c));
  assert(!lock.pin(&c.attributes));
  assert(!lock.cancellation_completed(c));
  assert(lock.finish_reaping(c));
}

static void cancel(M::ThreadControl &c, int expected, unsigned notifications) {
  unsigned pids = pid_calls, signals = signal_calls;
  errno = EDOM;
  MMIXPthread handle{};
  handle.__attrib = &c.attributes;
  assert(LIBC_NAMESPACE::pthread_cancel(handle) == expected);
  assert(errno == EDOM && signal_mask == 0x1234);
  assert(pid_calls == pids + notifications);
  assert(signal_calls == signals + notifications);
}

static void completed_states() {
  int token;
  void *results[] = {&token, nullptr, PTHREAD_CANCELED};
  for (void *result : results) {
    for (bool claimed : {false, true}) {
      M::ThreadControl c;
      publish(c);
      if (result == PTHREAD_CANCELED)
        c.result_kind = ThreadResultKind::Canceled;
      complete(c, result);
      {
        M::ThreadRegistryLock lock(M::thread_registry);
        assert(!lock.cancellation_completed(c)); // Requires an API pin.
        if (claimed)
          assert(lock.pin(&c.attributes) == &c && lock.claim_join(c));
      }
      auto kind = c.result_kind;
      // ExitReady precedes kernel clear; neither state may signal the old TID.
      for (unsigned clear : {42u, 0u}) {
        c.clear_tid.value.store(clear);
        cancel(c, 0, 0);
        cancel(c, 0, 0);
        assert(c.result_kind == kind &&
               c.attributes.retval.posix_retval == result);
        assert(c.cancellation.terminal() && !c.cancellation.ready());
        assert(!c.cancellation.enabled() && !c.cancellation.asynchronous());
      }
      {
        M::ThreadRegistryLock lock(M::thread_registry);
        if (!claimed)
          assert(lock.pin(&c.attributes) == &c && lock.claim_join(c));
        ThreadReturnValue joined;
        assert(lock.join_result(c, joined) && joined.posix_retval == result);
        assert(lock.release_join(c) && lock.unpin(c));
      }
      reap(c);
    }
  }
}

static void retained_states() {
  for (bool detached : {false, true}) {
    M::ThreadControl c;
    publish(c, detached, false, false);
    cancel(c, 0, 1); // Starting retains request and notification behavior.
    {
      M::ThreadRegistryLock lock(M::thread_registry);
      assert(lock.start(c));
    }
    cancel(c, 0, 1);
    assert(c.cancellation.ready());
    c.cancellation.set_enabled(false);
    cancel(c, 0, 0);
    c.cancellation.set_enabled(true);
    signal_result = -ESRCH;
    cancel(c, ESRCH, 1); // Live notification errors are not suppressed.
    signal_result = 0;
    {
      M::ThreadRegistryLock lock(M::thread_registry);
      assert(lock.begin_cleanup(c));
    }
    cancel(c, ESRCH, 0); // Cleaning, even while leases remain open.
    {
      M::ThreadRegistryLock lock(M::thread_registry);
      assert(lock.close_leases(c));
    }
    cancel(c, ESRCH, 0);
    {
      M::ThreadRegistryLock lock(M::thread_registry);
      assert(lock.exit_ready(c, ThreadReturnValue(nullptr)));
    }
    cancel(c, detached ? ESRCH : 0, 0);
    reap(c);
  }
  M::ThreadControl helper;
  publish(helper, true, true);
  cancel(helper, ESRCH, 0);
  M::current_control = &helper;
  cancel(helper, ESRCH, 0);
  M::current_control = nullptr;
  complete(helper, nullptr);
  cancel(helper, ESRCH, 0);
  reap(helper);

  M::ThreadControl self;
  publish(self);
  M::current_control = &self;
  cancel(self, 0, 0); // Deferred self cancellation does not notify.
  assert(self.cancellation.ready());
  M::current_control = nullptr;
  complete(self, nullptr);
  reap(self);
}

static void pin_and_reap(bool detach) {
  M::ThreadControl c;
  publish(c);
  complete(c, nullptr);
  c.clear_tid.value.store(0);
  {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.pin(&c.attributes) == &c); // Independent reader pin.
  }
  std::thread owner([&] {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.pin(&c.attributes) == &c);
    assert(detach ? lock.detach(c) : lock.claim_join(c));
    assert(lock.unpin(c) && lock.begin_reaping(c));
    assert(!lock.finish_reaping(c)); // Reader pin still protects storage.
  });
  owner.join();
  cancel(c, ESRCH, 0); // Unlisted but deliberately not yet reclaimed.
  M::ThreadRegistryLock lock(M::thread_registry);
  assert(!lock.cancellation_completed(c));
  assert(lock.unpin(c) && lock.finish_reaping(c));
}

static void completed_return_race(bool detach) {
  M::ThreadControl c;
  publish(c);
  complete(c, nullptr);
  Gate gate;
  std::thread sender([&] {
    return_gate = &gate;
    cancel(c, 0, 0);
    return_gate = nullptr;
  });
  // The no-op has dropped its pin and unlocked, but has not returned yet.
  gate.wait();
  if (detach) {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.pin(&c.attributes) == &c && lock.detach(c) && lock.unpin(c));
  }
  reap(c); // Must not find a leaked cancellation pin or sender lease.
  gate.release();
  sender.join();
}

static void sender_lease_race() {
  M::ThreadControl c;
  publish(c);
  Gate gate;
  std::thread sender([&] {
    notification_gate = &gate;
    cancel(c, 0, 1);
    notification_gate = nullptr;
  });
  gate.wait();
  {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.begin_cleanup(c) && lock.close_leases(c));
    assert(!lock.leases_drained(c));
    assert(!lock.exit_ready(c, ThreadReturnValue(nullptr)));
    assert(!lock.begin_reaping(c));
  }
  gate.release();
  sender.join();
  {
    M::ThreadRegistryLock lock(M::thread_registry);
    assert(lock.leases_drained(c));
    assert(lock.exit_ready(c, ThreadReturnValue(nullptr)));
  }
  cancel(c, 0, 0);
  reap(c);
}

int main() {
  completed_states();
  retained_states();
  for (bool detach : {false, true}) {
    pin_and_reap(detach);
    completed_return_race(detach);
  }
  sender_lease_race();
}
