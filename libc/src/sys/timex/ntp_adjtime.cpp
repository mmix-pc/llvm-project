// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/sys/timex/ntp_adjtime.h"
#include "src/sys/timex/adjtimex.h"
#include "src/__support/common.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, ntp_adjtime, (struct timex *buffer)) {
  return LIBC_NAMESPACE::adjtimex(buffer);
}
} // namespace LIBC_NAMESPACE_DECL
