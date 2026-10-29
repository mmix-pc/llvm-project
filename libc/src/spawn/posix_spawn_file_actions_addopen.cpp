//===-- Implementation of posix_spawn_file_actions_addopen ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "posix_spawn_file_actions_addopen.h"

#include "file_actions.h"
#include "src/__support/CPP/new.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "src/string/memory_utils/inline_memcpy.h"
#include "src/string/string_utils.h"

#include <spawn.h>

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, posix_spawn_file_actions_addopen,
                   (posix_spawn_file_actions_t *__restrict actions, int fd,
                    const char *__restrict path, int oflag, mode_t mode)) {
  if (actions == nullptr)
    return EINVAL;
  if (int error = check_spawn_file_action_fds(fd, fd))
    return error;

  AllocChecker ac;
  size_t path_size = internal::string_length(path) + 1;
  auto *owned_path = new (ac) char[path_size];
  if (!ac)
    return ENOMEM;
  inline_memcpy(owned_path, path, path_size);

  auto *act = new (ac) SpawnFileOpenAction(owned_path, fd, oflag, mode);
  if (!ac) {
    delete[] owned_path;
    return ENOMEM;
  }
  BaseSpawnFileAction::add_action(actions, act);

  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
