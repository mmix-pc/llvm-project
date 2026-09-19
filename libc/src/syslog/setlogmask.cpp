//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/syslog/setlogmask.h"
#include "src/__support/common.h"
#include "src/syslog/log.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, setlogmask, (int mask)) {
  return local_log::set_mask(mask);
}
} // namespace LIBC_NAMESPACE_DECL
