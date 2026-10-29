//===-- posix_spawnattr_setpgroup implementation ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "posix_spawnattr_setpgroup.h"

#include "src/__support/common.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, posix_spawnattr_setpgroup,
                   (posix_spawnattr_t * attr, pid_t pgroup)) {
  // Whether this group can be joined is checked when applying the attribute.
  attr->__pgrp = pgroup;
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
