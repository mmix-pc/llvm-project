//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the definitions of the tzname, timezone, and daylight
/// variables.
///
//===----------------------------------------------------------------------===//

#include "src/time/tz_variables.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

using TZNameArrayType = char *[2];
namespace {
char utc_name[] = "UTC";
char empty_name[] = "";
} // namespace

LLVM_LIBC_VARIABLE(TZNameArrayType, tzname) = {utc_name, empty_name};
LLVM_LIBC_VARIABLE(long, timezone) = 0;
LLVM_LIBC_VARIABLE(int, daylight) = 0;

} // namespace LIBC_NAMESPACE_DECL
