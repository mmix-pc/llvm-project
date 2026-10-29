//===-- Linux login name from the terminal accounting record --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/getlogin_r.h"

#include "hdr/errno_macros.h"
#include "hdr/limits_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/unistd/ttyname_r.h"
#include "src/utmp/utmp_engine.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, getlogin_r, (char *name, size_t size)) {
  struct ErrnoGuard {
    int saved = libc_errno;
    ~ErrnoGuard() { libc_errno = saved; }
  } guard;
  char terminal[PATH_MAX];
  int error = LIBC_NAMESPACE::ttyname_r(0, terminal, sizeof(terminal));
  if (error)
    return error;
  constexpr char prefix[] = "/dev/";
  for (size_t i = 0; i != sizeof(prefix) - 1; ++i)
    if (terminal[i] != prefix[i])
      return ENOENT;
  return utmp_backend::login_name(terminal + sizeof(prefix) - 1, name, size);
}
} // namespace LIBC_NAMESPACE_DECL
