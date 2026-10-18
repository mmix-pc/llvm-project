//===-- Internal local-time conversion interface ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_TIME_TIME_ZONE_H
#define LLVM_LIBC_SRC_TIME_TIME_ZONE_H

#include "hdr/types/struct_tm.h"
#include "hdr/types/time_t.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace time_utils {

// Refreshes POSIX TZ state and public variables. Invalid TZ is never replaced
// with UTC. Environment mutation must be synchronized by the caller, just as
// for getenv. Concurrent conversions and tzset calls are internally serialized.
ErrorOr<void> refresh_timezone();
ErrorOr<tm *> localtime_internal(const time_t *timer, tm *result);
ErrorOr<time_t> mktime_local(tm *result);

// Supplies missing timezone metadata for user-constructed tm values. Existing
// metadata from localtime/gmtime is preserved. A negative tm_isdst remains
// unknown unless an epoch conversion explicitly requires inference.
ErrorOr<void> prepare_timezone_for_format(tm *result, bool epoch);

} // namespace time_utils
} // namespace LIBC_NAMESPACE_DECL

#endif
