//===-- Internal Linux spawn interface -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC_SPAWN_LINUX_SPAWN_H
#define LLVM_LIBC_SRC_SPAWN_LINUX_SPAWN_H

#include "hdr/errno_macros.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/macros/config.h"
#include <spawn.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

// Paths and their null-terminated pointer array are prepared before cloning.
// Providers copy the address space (no CLONE_VM), so the parent can release
// this storage as soon as spawn returns, independently of child progress.
int spawn(pid_t *pid, const char *const *paths,
          const posix_spawn_file_actions_t *actions,
          const posix_spawnattr_t *attr, char *const *argv, char *const *envp);

inline int spawn_exec(const char *const *paths, char *const *argv,
                      char *const *envp) {
  bool denied = false;
  for (; *paths; ++paths) {
    long result = syscall_impl<long>(SYS_execve, *paths, argv, envp);
    // Do not invoke a shell on ENOEXEC. Only failed path lookups and access
    // denials allow trying another candidate; no allocation or libc locks here.
    if (result == -EACCES)
      denied = true;
    else if (result != -ENOENT && result != -ENOTDIR)
      return result < 0 ? static_cast<int>(-result) : EIO;
  }
  return denied ? EACCES : ENOENT;
}

} // namespace LIBC_NAMESPACE_DECL
#endif
