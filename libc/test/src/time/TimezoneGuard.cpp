//===-- Scoped timezone environment for time tests ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "test/src/time/TimezoneGuard.h"
#include "src/__support/CPP/string_view.h"
#if defined(__linux__) && defined(LIBC_FULL_BUILD)
#include "config/app.h"
#include "src/stdlib/environ_internal.h"
namespace LIBC_NAMESPACE_DECL {
AppProperties app;
}
#elif defined(__linux__)
#include "hdr/stdlib_overlay.h"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace testing {

TimezoneGuard::TimezoneGuard(bool activate) {
  if (activate)
    save();
}

void TimezoneGuard::save() {
  if (active)
    return;
  const char *value = nullptr;
#if defined(__linux__) && defined(LIBC_FULL_BUILD)
  value = internal::EnvironmentManager::get_instance().get("TZ");
#elif defined(__linux__)
  value = ::getenv("TZ");
#endif
  present = value != nullptr;
  saved = cpp::string_view(value ? value : "");
  active = true;
  set("UTC0");
}

TimezoneGuard::~TimezoneGuard() { restore(); }

void TimezoneGuard::restore() {
  if (!active)
    return;
  set(present ? saved.c_str() : nullptr);
  active = false;
}

bool TimezoneGuard::set(const char *value) {
#if defined(__linux__) && defined(LIBC_FULL_BUILD)
  auto &environment = internal::EnvironmentManager::get_instance();
  return (value ? environment.set("TZ", value, true)
                : environment.unset("TZ")) == 0;
#elif defined(__linux__)
  return (value ? ::setenv("TZ", value, 1) : ::unsetenv("TZ")) == 0;
#else
  (void)value;
  return true;
#endif
}

} // namespace testing
} // namespace LIBC_NAMESPACE_DECL
