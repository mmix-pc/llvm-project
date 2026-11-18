// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/time/timegm.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/null_check.h"
#include "src/time/time_utils.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(time_t, timegm, (struct tm *value)) {
  LIBC_CRASH_ON_NULLPTR(value);
  auto seconds = time_utils::mktime_internal(value);
  if (!seconds) {
    libc_errno = EOVERFLOW;
    return static_cast<time_t>(-1);
  }
  tm normalized = {};
  auto result = time_utils::gmtime_internal(&*seconds, &normalized);
  if (!result) {
    libc_errno = result.error();
    return static_cast<time_t>(-1);
  }
  *value = normalized;
  return *seconds;
}
} // namespace LIBC_NAMESPACE_DECL
