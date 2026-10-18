//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the definition of the tzset function.
///
//===----------------------------------------------------------------------===//

#include "src/time/tzset.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "src/time/time_zone.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(void, tzset, ()) {
  auto status = time_utils::refresh_timezone();
  if (!status)
    libc_errno = status.error();
}

} // namespace LIBC_NAMESPACE_DECL
