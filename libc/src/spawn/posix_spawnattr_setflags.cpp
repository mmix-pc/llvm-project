//===-- posix_spawnattr_setflags implementation ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "posix_spawnattr_setflags.h"

#include "hdr/errno_macros.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, posix_spawnattr_setflags,
                   (posix_spawnattr_t * attr, short flags)) {
  constexpr int ALL_FLAGS = POSIX_SPAWN_RESETIDS | POSIX_SPAWN_SETPGROUP |
                            POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK |
                            POSIX_SPAWN_SETSCHEDPARAM |
                            POSIX_SPAWN_SETSCHEDULER | POSIX_SPAWN_SETSID;
  if (flags & ~ALL_FLAGS)
    return EINVAL;
  attr->__flags = flags;
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
