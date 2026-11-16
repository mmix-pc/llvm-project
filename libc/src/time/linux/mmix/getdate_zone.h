//===-- MMIX getdate timezone candidates -------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC_TIME_LINUX_MMIX_GETDATE_ZONE_H
#define LLVM_LIBC_SRC_TIME_LINUX_MMIX_GETDATE_ZONE_H
#include "hdr/stdint_proxy.h"
#include "hdr/types/size_t.h"
#include "src/__support/CPP/optional.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
namespace time_utils {
struct GetdateZone {
  int64_t offset;
  bool daylight;
  const char *name;
};
// Enumerates historical and recurring types. Returned names are interned and
// remain valid after a concurrent explicit timezone refresh.
ErrorOr<cpp::optional<GetdateZone>> getdate_timezone_type(size_t index);
} // namespace time_utils
} // namespace LIBC_NAMESPACE_DECL
#endif
