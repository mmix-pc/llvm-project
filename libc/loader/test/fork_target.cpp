//===-- MMIX loader fork inheritance probe --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/locks.h"
#include "loader/linux/syscall.h"
#include <errno.h>
#include <signal.h>
#include <sys/syscall.h>

using namespace LIBC_NAMESPACE::loader;
static void check(bool ok) {
  if (!ok) {
    static const char message[] = "LOADER_FORK FAIL\n";
    raw_syscall(SYS_write, 1, reinterpret_cast<long>(message), sizeof(message)-1);
    raw_syscall(SYS_exit_group, 1);
    __builtin_trap();
  }
}
#ifndef FORK_CHILD_IMAGE
static LoaderLocks locks;
static unsigned suppressed;
static LoaderCancellation hooks{
    [](uint64_t *saved) -> long { *saved = suppressed++; return 0; },
    [](uint64_t saved) -> long { suppressed = saved; return 0; }};
#endif

extern "C" [[noreturn]] void _start() {
#ifdef FORK_CHILD_IMAGE
  // exec replaced the frozen loader instance with a new address space.
  static const char message[] = "LOADER_FORK EXEC PASS\n";
#else
  uint64_t saved;
  check(!hooks.enter(&saved));
  check(!locks.prepare_fork());
  locks.finish_fork(false); // Kernel-failure rollback uses the parent path.
  for (unsigned mode = 0; mode != 2; ++mode) {
    check(!locks.prepare_fork());
    // Exercise the loader hooks around the admitted same-stack kernel clone;
    // this is not an export or qualification of the public pthread fork API.
    long child = raw_syscall(SYS_clone, SIGCHLD, 0, 0, 0, 0);
    check(child >= 0);
    locks.finish_fork(child == 0);
    if (!child) {
      {
        LoaderLocks::Session session(locks, hooks);
        LoaderLocks::Guard guard(session, LoaderLocks::Kind::Mutation);
        check(session.error() == -ENOTSUP && guard.error() == -ENOTSUP);
        check(locks.prepare_fork() == -ENOTSUP && suppressed == 1);
      }
      if (mode) {
        const char *args[] = {"/fork-child", nullptr};
        const char *env[] = {nullptr};
        raw_syscall(SYS_execve, reinterpret_cast<long>(args[0]),
                    reinterpret_cast<long>(args), reinterpret_cast<long>(env));
        check(false);
      }
      raw_syscall(SYS_exit_group, 0);
      __builtin_trap();
    }
    int status = -1;
    check(raw_syscall(SYS_wait4, child, reinterpret_cast<long>(&status), 0, 0)
              == child && !status);
    LoaderLocks::Session session(locks, hooks);
    LoaderLocks::Guard guard(session, LoaderLocks::Kind::Mutation);
    check(!session.error() && !guard.error());
  }
  check(!hooks.leave(saved) && !suppressed);
  static const char message[] = "LOADER_FORK PASS children=2 rollback=1\n";
#endif
  check(raw_syscall(SYS_write, 1, reinterpret_cast<long>(message),
                    sizeof(message)-1) == sizeof(message)-1);
  raw_syscall(SYS_exit_group, 0);
  __builtin_trap();
}
