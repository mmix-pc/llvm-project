//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/syslog/vsyslog.h"
#include "src/__support/common.h"
#include "src/syslog/log.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(void, vsyslog,
                   (int priority, const char *format, va_list args)) {
  local_log::write(priority, format, args);
}
} // namespace LIBC_NAMESPACE_DECL
