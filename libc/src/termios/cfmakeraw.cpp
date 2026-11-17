//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/termios/cfmakeraw.h"
#include "hdr/termios_macros.h"
#include "src/__support/common.h"
#include "src/__support/macros/null_check.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(void, cfmakeraw, (termios * t)) {
  LIBC_CRASH_ON_NULLPTR(t);
  t->c_iflag &=
      ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
  t->c_oflag &= ~OPOST;
  t->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
  t->c_cflag = (t->c_cflag & ~(CSIZE | PARENB)) | CS8;
  t->c_cc[VMIN] = 1;
  t->c_cc[VTIME] = 0;
}

} // namespace LIBC_NAMESPACE_DECL
