//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Linux implementation of cfsetispeed.
///
//===----------------------------------------------------------------------===//

#include "src/termios/cfsetispeed.h"
#include "hdr/types/struct_termios.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/null_check.h"
#include "src/termios/linux/speed_utils.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, cfsetispeed, (termios * t, speed_t speed)) {
  LIBC_CRASH_ON_NULLPTR(t);
  if (!is_valid_speed(speed)) {
    libc_errno = EINVAL;
    return -1;
  }

  t->c_ispeed = speed;
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
