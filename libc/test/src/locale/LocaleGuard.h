//===-- Scoped locale for encoding tests -------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_TEST_SRC_LOCALE_LOCALEGUARD_H
#define LLVM_LIBC_TEST_SRC_LOCALE_LOCALEGUARD_H

#ifdef LIBC_FULL_BUILD
#include "src/locale/locale.h"
// Test-only stack ownership avoids unrelated allocation/environment
// dependencies in codec tests. Public object ownership is covered by
// builtin_locale_test.
class LocaleGuard {
  __locale_t object;
  locale_t previous;

public:
  explicit LocaleGuard(bool utf8 = true)
      : object(LIBC_NAMESPACE::internal::locale_from_mask(
            utf8 ? LIBC_NAMESPACE::internal::LOCALE_MASK : 0)),
        previous(LIBC_NAMESPACE::internal::thread_locale) {
    LIBC_NAMESPACE::internal::thread_locale = &object;
  }
  ~LocaleGuard() { LIBC_NAMESPACE::internal::thread_locale = previous; }
  LocaleGuard(const LocaleGuard &) = delete;
  LocaleGuard &operator=(const LocaleGuard &) = delete;
};
#else
class LocaleGuard {
public:
  explicit LocaleGuard(bool = true) {}
};
#endif
#endif
