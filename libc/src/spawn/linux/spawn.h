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
#include "src/__support/macros/properties/architectures.h"
#include <spawn.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {

// Pipe streams require setup failures to remain child exit status, even when
// the provider diagnoses public spawn failures synchronously.
enum class SpawnErrorPolicy { ProviderDefault, ChildStatus };

// Paths and their null-terminated pointer array are prepared before cloning.
// Providers copy the address space (no CLONE_VM), so the parent can release
// this storage as soon as spawn returns, independently of child progress.
int spawn(pid_t *pid, const char *const *paths,
          const posix_spawn_file_actions_t *actions,
          const posix_spawnattr_t *attr, char *const *argv, char *const *envp,
          SpawnErrorPolicy policy = SpawnErrorPolicy::ProviderDefault);

// Protect wrapper-owned storage as well as the inner spawn transaction. The
// saved mask is also the child's inherited mask, not the temporary parent mask.
class SpawnSignalMask {
#ifdef LIBC_TARGET_ARCH_IS_MMIX
  sigset_t saved{};
  int error_code = 0;

public:
  SpawnSignalMask();
  ~SpawnSignalMask() noexcept(false);
  int error() const { return error_code; }
  const sigset_t &original() const { return saved; }
#else
public:
  SpawnSignalMask() = default;
  int error() const { return 0; }
#endif
  SpawnSignalMask(const SpawnSignalMask &) = delete;
  SpawnSignalMask &operator=(const SpawnSignalMask &) = delete;
};

#ifdef LIBC_TARGET_ARCH_IS_MMIX
int spawn(pid_t *pid, const char *const *paths,
          const posix_spawn_file_actions_t *actions,
          const posix_spawnattr_t *attr, char *const *argv, char *const *envp,
          const SpawnSignalMask &mask);
#else
inline int spawn(pid_t *pid, const char *const *paths,
                 const posix_spawn_file_actions_t *actions,
                 const posix_spawnattr_t *attr, char *const *argv,
                 char *const *envp, const SpawnSignalMask &) {
  return spawn(pid, paths, actions, attr, argv, envp);
}
#endif

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
