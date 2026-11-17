// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/unistd/setpgrp.h"
#include "src/unistd/setpgid.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, setpgrp, ()) {
  return LIBC_NAMESPACE::setpgid(0, 0);
}
} // namespace LIBC_NAMESPACE_DECL
