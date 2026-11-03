//===-- MMIX Linux str2sig implementation --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/signal/str2sig.h"
#include "signal_names.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, str2sig, (const char *str, int *signum)) {
  int number = mmix::signal_from_name(str);
  if (number < 0)
    return -1;
  *signum = number;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
