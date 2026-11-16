//===-- MMIX timezone file view ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC_TIME_LINUX_MMIX_TZIF_H
#define LLVM_LIBC_SRC_TIME_LINUX_MMIX_TZIF_H
#include "hdr/stdint_proxy.h"
#include "hdr/types/size_t.h"
#include "src/__support/CPP/string_view.h"
#include "src/time/posix_timezone.h"
namespace LIBC_NAMESPACE_DECL {
namespace time_utils {
struct ZoneType {
  int64_t offset;
  bool daylight;
  cpp::string_view name;
};
// The view borrows immutable file bytes. Owners must retain them through every
// conversion; public tm_zone strings require separately retained storage.
class TZifView {
  const unsigned char *transitions = nullptr;
  const unsigned char *indices = nullptr;
  const unsigned char *types = nullptr;
  const char *names = nullptr;
  const unsigned char *standard_flags = nullptr, *ut_flags = nullptr;
  bool has_standard_flags = false, has_ut_flags = false;
  size_t count = 0, type_count = 0, name_size = 0;
  unsigned width = 4;
  PosixTimeZone footer;
  bool has_footer = false;

public:
  enum class Result { SUCCESS, INVALID, LEAP_RECORDS };
  // A failed parse never modifies the supplied view.
  static Result parse(const unsigned char *data, size_t size, TZifView &view);
  ZoneType type(size_t index) const;
  ZoneType at(int64_t seconds) const;
  // Supply the transition schedule of a default-rule file using the caller's
  // names and offsets. Transition clock indicators are respected.
  ZoneType at_default(int64_t seconds, const PosixTimeZone &requested) const;
  bool discontinuity(int64_t wall, ZoneType &before, ZoneType &after) const;
  PosixTimeZone nearby_rules(int64_t seconds) const;
  size_t types_size() const { return type_count; }
  bool footer_present() const { return has_footer; }
  const PosixTimeZone &future_rules() const { return footer; }
};
} // namespace time_utils
} // namespace LIBC_NAMESPACE_DECL
#endif
