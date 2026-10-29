//===-- Spawn file actions  -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SPAWN_FILE_ACTIONS_H
#define LLVM_LIBC_SRC_SPAWN_FILE_ACTIONS_H

#include "hdr/stdint_proxy.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "src/unistd/sysconf.h"
#include <spawn.h> // For mode_t

namespace LIBC_NAMESPACE_DECL {

// Only open and dup2 actions have an upper descriptor bound at registration.
inline int check_spawn_file_action_fds(int fd, int newfd) {
  if (fd < 0 || newfd < 0)
    return EBADF;

  int saved_errno = libc_errno;
  libc_errno = 0;
  long open_max = LIBC_NAMESPACE::sysconf(_SC_OPEN_MAX);
  int error = libc_errno;
  libc_errno = saved_errno;
  // An indeterminate limit has no upper bound; a failed query is not one.
  if (open_max == -1)
    return error;
  return fd >= open_max || newfd >= open_max ? EBADF : 0;
}

struct BaseSpawnFileAction {
  enum ActionType {
    OPEN = 111,
    CLOSE = 222,
    DUP2 = 333,
    CHDIR = 444,
    FCHDIR = 555,
  };

  ActionType type;
  BaseSpawnFileAction *next;

  static void add_action(posix_spawn_file_actions_t *actions,
                         BaseSpawnFileAction *act) {
    if (actions->__back != nullptr) {
      auto *back = reinterpret_cast<BaseSpawnFileAction *>(actions->__back);
      back->next = act;
      actions->__back = act;
    } else {
      // First action is being added.
      actions->__front = actions->__back = act;
    }
  }

protected:
  explicit BaseSpawnFileAction(ActionType t) : type(t), next(nullptr) {}
};

struct SpawnFileOpenAction : public BaseSpawnFileAction {
  char *path; // Owned array, released by posix_spawn_file_actions_destroy.
  int fd;
  int oflag;
  mode_t mode;

  SpawnFileOpenAction(char *p, int fdesc, int flags, mode_t m)
      : BaseSpawnFileAction(BaseSpawnFileAction::OPEN), path(p), fd(fdesc),
        oflag(flags), mode(m) {}
};

struct SpawnFileCloseAction : public BaseSpawnFileAction {
  int fd;

  SpawnFileCloseAction(int fdesc)
      : BaseSpawnFileAction(BaseSpawnFileAction::CLOSE), fd(fdesc) {}
};

struct SpawnFileDup2Action : public BaseSpawnFileAction {
  int fd;
  int newfd;

  SpawnFileDup2Action(int fdesc, int new_fdesc)
      : BaseSpawnFileAction(BaseSpawnFileAction::DUP2), fd(fdesc),
        newfd(new_fdesc) {}
};

struct SpawnFileChdirAction : public BaseSpawnFileAction {
  char *path; // Owned array, released by posix_spawn_file_actions_destroy.

  SpawnFileChdirAction(char *p)
      : BaseSpawnFileAction(BaseSpawnFileAction::CHDIR), path(p) {}
};

struct SpawnFileFchdirAction : public BaseSpawnFileAction {
  int fd;

  SpawnFileFchdirAction(int fdesc)
      : BaseSpawnFileAction(BaseSpawnFileAction::FCHDIR), fd(fdesc) {}
};

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_SPAWN_FILE_ACTIONS_H
