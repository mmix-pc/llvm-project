//===-- Module ownership tests -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/module_ownership.h"
#include "loader/linux/syscall.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>

using namespace LIBC_NAMESPACE::loader;
static bool fail_map;
static void check(bool value, int line = __builtin_LINE()) {
  if (!value) {
    fprintf(stderr, "ownership failure at %d\n", line);
    abort();
  }
}
namespace LIBC_NAMESPACE::loader {
long raw_syscall(long n, long a, long b, long c, long d, long e, long f) {
  if (n == SYS_mmap && fail_map)
    return -ENOMEM;
  check(n == SYS_mmap || n == SYS_munmap);
  long result = ::syscall(n, a, b, c, d, e, f);
  return result == -1 ? -errno : result;
}
} // namespace LIBC_NAMESPACE::loader

extern "C" int main() {
  Arena arena(sysconf(_SC_PAGESIZE));
  using Kind = ModuleOwnership::Kind;
  ModuleOwnership module(3, 7, false);
  ModuleOwnership::Token h, edge, lease;
  h.ticket = 99;
  fail_map = true;
  check(module.acquire(arena, Kind::Handle, h) == -ENOMEM);
  check(!module.owners() && h.ticket == 99);
  fail_map = false;
  check(!module.acquire(arena, Kind::Handle, h) && h.ticket == 1);
  check(!module.acquire(arena, Kind::Dependency, edge));
  check(!module.acquire(arena, Kind::Runtime, lease));
  check(module.owners() == 3 && module.owners(Kind::Dependency) == 1);
  auto stale = lease;
  ++stale.generation;
  check(module.release(stale) == -EINVAL && module.owners() == 3);
  stale = lease;
  ++stale.image;
  check(module.release(stale) == -EINVAL);
  stale = lease;
  stale.kind = Kind::Handle;
  check(module.release(stale) == -EINVAL);
  check(!module.release(h));
  check(module.release(h) == -EINVAL);
  auto copy = h;
  check(!module.acquire(arena, Kind::Handle, h) && h.ticket != copy.ticket);
  check(module.release(copy) == -EINVAL && module.owners() == 3);
  check(!module.retire());
  check(module.acquire(arena, Kind::Runtime, copy) == -EBUSY);
  check(module.mark_reclaimed() == -EBUSY);
  check(!module.release(edge) && !module.release(lease) && !module.release(h));
  check(!module.mark_reclaimed());
  check(module.retire() == -EBUSY && module.release(h) == -EINVAL);
  check(module.acquire(arena, Kind::Handle, h) == -EBUSY);

  ModuleOwnership process(4, 1, false);
  check(!process.acquire(arena, Kind::ProcessCallback, h));
  check(process.owners(Kind::ProcessCallback) == 1 &&
        !process.owners(Kind::Runtime));
  check(!process.retire() && process.mark_reclaimed() == -EBUSY);
  check(!process.release(h) && !process.mark_reclaimed());

  ModuleOwnership signal(5, 1, false);
  check(!signal.retain_signal() && signal.owners() == 1);
  check(signal.retire() == -EBUSY);
  signal.finish_signal(false);
  check(!signal.owners() && !signal.is_permanent());
  check(!signal.retire() && signal.retain_signal() == -EBUSY);
  signal.cancel_retirement();
  check(!signal.retain_signal());
  signal.finish_signal(true);
  check(!signal.owners() && signal.is_permanent());
  check(signal.retire() == -EBUSY);

  for (unsigned root = 0; root != 2; ++root) {
    // Startup and NODELETE share the same irreversible root policy.
    ModuleOwnership permanent(10 + root, 1, true);
    check(permanent.is_permanent() && permanent.retire() == -EBUSY);
    check(!permanent.acquire(arena, Kind::Handle, h));
    check(!permanent.release(h) && permanent.mark_reclaimed() == -EBUSY);
  }
  ModuleOwnership bounded(20, 2, false, 2);
  check(!bounded.acquire(arena, Kind::Handle, h));
  check(!bounded.acquire(arena, Kind::Runtime, lease));
  auto unchanged = copy;
  check(bounded.acquire(arena, Kind::Dependency, copy) == -EOVERFLOW);
  check(copy.ticket == unchanged.ticket && bounded.owners() == 2);
  check(!bounded.release(h) && !bounded.release(lease));
  check(bounded.acquire(arena, Kind::Handle, h) == -EOVERFLOW);
  ModuleOwnership invalid(0, 1, false);
  check(invalid.acquire(arena, Kind::Handle, h) == -EINVAL);
  check(bounded.acquire(arena, static_cast<Kind>(99), h) == -EINVAL);

  // Exercise both serialized linearizations with real competing callers.
  for (unsigned acquire_first = 0; acquire_first != 2; ++acquire_first) {
    ModuleOwnership racing(30, 4, false);
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    check(!pthread_mutex_lock(&mutex));
    ModuleOwnership::Token token;
    if (acquire_first)
      check(!racing.acquire(arena, Kind::Runtime, token));
    else
      check(!racing.retire());
    struct Call {
      pthread_mutex_t *mutex;
      ModuleOwnership *module;
      Arena *arena;
      ModuleOwnership::Token *token;
      bool retire;
      long result;
    } call{&mutex, &racing, &arena, &token, bool(acquire_first), 99};
    pthread_t contender;
    check(!pthread_create(
        &contender, nullptr,
        [](void *p) -> void * {
          auto &c = *static_cast<Call *>(p);
          check(!pthread_mutex_lock(c.mutex));
          c.result = c.retire
                         ? c.module->retire()
                         : c.module->acquire(*c.arena, Kind::Runtime, *c.token);
          check(!pthread_mutex_unlock(c.mutex));
          return nullptr;
        },
        &call));
    check(!pthread_mutex_unlock(&mutex));
    check(!pthread_join(contender, nullptr));
    check(!pthread_mutex_destroy(&mutex));
    check(call.result == (acquire_first ? 0 : -EBUSY));
    if (acquire_first) {
      check(racing.mark_reclaimed() == -EBUSY);
      check(!racing.release(token));
    }
    check(!racing.mark_reclaimed());
  }
  check(!arena.reset());
  return 0;
}
