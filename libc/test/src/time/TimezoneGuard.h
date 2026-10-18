//===-- Scoped timezone environment for time tests --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TEST_SRC_TIME_TIMEZONE_GUARD_H
#define LLVM_LIBC_TEST_SRC_TIME_TIMEZONE_GUARD_H

#include "src/__support/CPP/string.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace testing {

class TimezoneGuard {
  cpp::string saved;
  bool present = false;
  bool active = false;

public:
  explicit TimezoneGuard(bool activate = true);
  ~TimezoneGuard();
  TimezoneGuard(const TimezoneGuard &) = delete;
  TimezoneGuard &operator=(const TimezoneGuard &) = delete;
  bool set(const char *value);
  void save();
  void restore();
};

} // namespace testing
} // namespace LIBC_NAMESPACE_DECL

#endif
