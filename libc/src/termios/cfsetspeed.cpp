//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/termios/cfsetspeed.h"
#include "src/__support/common.h"
#include "src/__support/macros/null_check.h"
#include "src/termios/cfsetispeed.h"
#include "src/termios/cfsetospeed.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, cfsetspeed, (termios * t, speed_t speed)) {
  LIBC_CRASH_ON_NULLPTR(t);
  // Do not publish one direction if the other setter rejects the speed.
  termios updated = *t;
  if (LIBC_NAMESPACE::cfsetospeed(&updated, speed) != 0 ||
      LIBC_NAMESPACE::cfsetispeed(&updated, speed) != 0)
    return -1;
  *t = updated;
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
