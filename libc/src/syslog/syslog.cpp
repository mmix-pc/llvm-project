//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/syslog/syslog.h"
#include "src/__support/common.h"
#include "src/syslog/log.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(void, syslog, (int priority, const char *format, ...)) {
  va_list args;
  va_start(args, format);
  local_log::write(priority, format, args);
  va_end(args);
}
} // namespace LIBC_NAMESPACE_DECL
