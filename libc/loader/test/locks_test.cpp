//===-- Loader lock tests -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/arena.h"
#include "loader/linux/locks.h"
#include "loader/linux/syscall.h"
#include <errno.h>
#include <linux/futex.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace LIBC_NAMESPACE::loader;
using LIBC_NAMESPACE::cpp::Atomic;
using LIBC_NAMESPACE::internal::ForkRecovery;
static_assert(!ForkRecovery().is_child());
static_assert(ForkRecovery().allows_fork());
static_assert(ForkRecovery().allows_thread_creation());
static_assert(ForkRecovery().allows_loader());
static_assert(!ForkRecovery::child().allows_loader());
static_assert(!ForkRecovery::child(true, false).allows_loader());
static_assert(!ForkRecovery::child(false, true).allows_loader());
static_assert(ForkRecovery::child(true, true).allows_loader());
static_assert(ForkRecovery::child(true, true).is_child());
static_assert(ForkRecovery::child(true, true).allows_fork());
static_assert(ForkRecovery::child(true, true).allows_thread_creation());
static thread_local unsigned disabled;
static thread_local long wait_error, enter_error;
static thread_local bool pending;
static Atomic<unsigned> deliveries{0}, waits{0};
static thread_local unsigned callbacks;
static volatile sig_atomic_t signal_seen;
static void check(bool value, int line = __builtin_LINE()) {
  if (!value) {
    fprintf(stderr, "loader lock check at %d\n", line);
    abort();
  }
}
static long enter(uint64_t *saved) {
  if (enter_error)
    return enter_error;
  *saved = disabled;
  ++disabled;
  return 0;
}
static long leave(uint64_t saved) {
  check(disabled == saved + 1);
  disabled = static_cast<unsigned>(saved);
  if (!disabled && pending) {
    pending = false;
    deliveries.fetch_add(1);
  }
  return 0;
}
static void begin_callback() {
  check(disabled);
  ++callbacks;
}
static void end_callback() {
  check(disabled && callbacks);
  --callbacks;
}
static LoaderCancellation hooks{enter, leave, begin_callback, end_callback};
namespace LIBC_NAMESPACE::loader {
long raw_syscall(long n, long a, long b, long c, long d, long e, long f) {
  check(disabled != 0);
  if (n == SYS_futex && b == FUTEX_WAIT_PRIVATE) {
    waits.fetch_add(1);
    if (wait_error) {
      long result = wait_error;
      wait_error = 0;
      return result;
    }
  }
  long result = ::syscall(n, a, b, c, d, e, f);
  return result == -1 ? -errno : result;
}
} // namespace LIBC_NAMESPACE::loader

static LoaderLocks locks;
using Kind = LoaderLocks::Kind;
static unsigned counter;
static unsigned published;
static void *fork_during_writer(void *) {
  uint64_t saved;
  check(!enter(&saved));
  bool quiescent = true;
  check(locks.prepare_fork(&quiescent) == -EAGAIN && !quiescent);
  check(!leave(saved));
  return nullptr;
}
static void *fork_after_writer(void *) {
  uint64_t saved;
  check(!enter(&saved));
  check(!locks.prepare_fork());
  check(published == 37);
  pid_t child = fork();
  check(child >= 0);
  locks.finish_fork(child == 0);
  if (!child) {
    LoaderLocks::Session session(locks, hooks);
    check(session.error() == -ENOTSUP && published == 37);
    _exit(0);
  }
  int status;
  check(waitpid(child, &status, 0) == child && status == 0);
  check(!leave(saved));
  return nullptr;
}
static void *worker(void *opaque) {
  auto &storage = *static_cast<Arena *>(opaque);
  for (unsigned i = 0; i < 16; ++i) {
    LoaderLocks::Session scope(locks, hooks);
    check(!scope.error());
    LoaderLocks::Guard mutation(scope, Kind::Mutation);
    check(!mutation.error());
    LoaderLocks::Guard recursive(scope, Kind::Mutation);
    check(!recursive.error());
    LoaderLocks::Guard registry(scope, Kind::Registry);
    check(!registry.error());
    LoaderLocks::Guard arena(scope, Kind::Arena);
    check(!arena.error());
    void *allocation;
    check(storage.allocate(32, 8, allocation) == 0);
    static_cast<char *>(allocation)[31] = 42;
    ++counter;
  }
  check(disabled == 0);
  return nullptr;
}
static void *failed_wait(void *) {
  wait_error = -EIO;
  {
    LoaderLocks::Session scope(locks, hooks);
    LoaderLocks::Guard mutation(scope, Kind::Mutation);
    check(mutation.error() == -EIO);
  }
  check(disabled == 0);
  return nullptr;
}
static void *interrupted_wait(void *) {
  wait_error = -EINTR;
  LoaderLocks::Session scope(locks, hooks);
  LoaderLocks::Guard mutation(scope, Kind::Mutation);
  check(!mutation.error());
  return nullptr;
}
extern "C" int main() {
  {
    LoaderLocks::Session invalid(locks, {});
    check(invalid.error() == -EINVAL && !disabled);
  }
  enter_error = -EIO;
  {
    LoaderLocks::Session invalid(locks, hooks);
    LoaderLocks::Guard guard(invalid, Kind::Mutation);
    check(guard.error() == -EIO && !disabled);
  }
  enter_error = 0;
  {
    LoaderLocks::Session scope(locks, hooks);
    LoaderLocks::Guard mutation(scope, Kind::Mutation);
    check(!mutation.error());
    check(locks.prepare_fork() == -EDEADLK);
    {
      LoaderLocks::Session::Callback callback(scope);
      check(callbacks == 1);
    }
    check(!callbacks);
    {
      // Reentrant callbacks can start another API session outside short locks.
      LoaderLocks::Session nested(locks, hooks);
      LoaderLocks::Guard recursive(nested, Kind::Mutation);
      check(!recursive.error() && disabled == 2);
    }
    pending = true;
    {
      LoaderLocks::Guard registry(scope, Kind::Registry);
      check(!registry.error());
      LoaderLocks::Guard inverted(scope, Kind::Mutation);
      LoaderLocks::Guard duplicate(scope, Kind::Registry);
      check(inverted.error() == -EDEADLK && duplicate.error() == -EDEADLK);
    }
    check(!deliveries.load());
    pthread_t thread;
    check(!pthread_create(&thread, nullptr, failed_wait, nullptr));
    check(!pthread_join(thread, nullptr));
  }
  check(deliveries.load() == 1 && disabled == 0 && waits.load() > 0);
  pthread_t interrupted;
  unsigned before_wait = waits.load();
  {
    LoaderLocks::Session scope(locks, hooks);
    LoaderLocks::Guard mutation(scope, Kind::Mutation);
    check(!pthread_create(&interrupted, nullptr, interrupted_wait, nullptr));
    while (waits.load() == before_wait)
      sched_yield();
  }
  check(!pthread_join(interrupted, nullptr));
  pthread_t forking;
  {
    LoaderLocks::Session scope(locks, hooks);
    LoaderLocks::Guard mutation(scope, Kind::Mutation);
    check(!pthread_create(&forking, nullptr, fork_during_writer, nullptr));
    check(!pthread_join(forking, nullptr));
    // The fork snapshot cannot observe a half-published writer transaction.
    published = 37;
  }
  fork_after_writer(nullptr); // Retry after the complete writer publication.
  struct sigaction action{};
  action.sa_handler = [](int) { signal_seen = 1; };
  sigemptyset(&action.sa_mask);
  check(!sigaction(SIGUSR1, &action, nullptr));
  uint64_t saved;
  check(!enter(&saved) && !locks.prepare_fork());
  check(!raise(SIGUSR1) && signal_seen);
  pid_t child = fork();
  check(child >= 0);
  if (!child) {
    locks.finish_fork(true);
    {
      LoaderLocks::Session scope(locks, hooks);
      LoaderLocks::Guard guard(scope, Kind::Mutation);
      check(scope.error() == -ENOTSUP && guard.error() == -ENOTSUP);
      check(locks.prepare_fork() == -ENOTSUP);
    }
    check(!leave(saved));
    _exit(0);
  }
  locks.finish_fork(false);
  check(!leave(saved));
  int status;
  check(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
        !WEXITSTATUS(status));
  check(!enter(&saved) && !locks.prepare_fork());
  locks.finish_fork(false); // Parent failure path has the same release.
  check(!leave(saved));
  bool quiescent = true;
  {
    // A detached callback still owns a session even without a mutation guard.
    LoaderLocks::Session scope(locks, hooks);
    check(!scope.error() && locks.prepare_fork(&quiescent) == -EAGAIN);
    check(!quiescent);
  }
  check(!enter(&saved) && !locks.prepare_fork(&quiescent) && quiescent);
  child = fork();
  check(child >= 0);
  locks.finish_fork(child == 0, true);
  {
    LoaderLocks::Session scope(locks, hooks);
    LoaderLocks::Guard guard(scope, Kind::Mutation);
    check(!scope.error() && !guard.error());
  }
  check(!leave(saved));
  if (!child)
    _exit(0);
  check(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
        !WEXITSTATUS(status));
  Arena storage(4096);
  pthread_t threads[4];
  for (auto &thread : threads)
    check(!pthread_create(&thread, nullptr, worker, &storage));
  for (auto &thread : threads)
    check(!pthread_join(thread, nullptr));
  check(counter == 64);
  {
    LoaderLocks::Session scope(locks, hooks);
    LoaderLocks::Guard arena(scope, Kind::Arena);
    check(!arena.error() && storage.reset() == 0);
  }
  return 0;
}
