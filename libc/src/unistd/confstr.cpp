//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Implementation of confstr
///
//===----------------------------------------------------------------------===//

#include "src/unistd/confstr.h"

#include "hdr/errno_macros.h"
#include "hdr/types/off_t.h"
#include "hdr/types/size_t.h"
#include "hdr/unistd_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(size_t, confstr, (int name, char *buf, size_t len)) {
  const char *value = nullptr;
  constexpr bool LP64 = sizeof(int) == 4 && sizeof(long) == 8 &&
                        sizeof(void *) == 8 && sizeof(off_t) == 8;
  switch (name) {
  case _CS_PATH:
    value = "/usr/bin:/bin";
    break;
  case _CS_V6_WIDTH_RESTRICTED_ENVS:
    value = LP64 ? "POSIX_V6_LP64_OFF64" : "";
    break;
  case _CS_V7_WIDTH_RESTRICTED_ENVS:
    value = LP64 ? "POSIX_V7_LP64_OFF64" : "";
    break;
#ifdef _CS_V8_WIDTH_RESTRICTED_ENVS
  case _CS_V8_WIDTH_RESTRICTED_ENVS:
    value = LP64 ? "POSIX_V8_LP64_OFF64" : "";
    break;
  case _CS_POSIX_V8_THREADS_CFLAGS:
  case _CS_POSIX_V8_THREADS_LDFLAGS:
#ifdef _CS_POSIX_V7_THREADS_CFLAGS
  case _CS_POSIX_V7_THREADS_CFLAGS:
  case _CS_POSIX_V7_THREADS_LDFLAGS:
#endif
    if (_POSIX_THREADS > 0)
      value = "-pthread";
    break;
  case _CS_POSIX_V8_LP64_OFF64_CFLAGS:
  case _CS_POSIX_V8_LP64_OFF64_LDFLAGS:
  case _CS_POSIX_V8_LP64_OFF64_LIBS:
#endif
  case _CS_POSIX_V6_LP64_OFF64_CFLAGS:
  case _CS_POSIX_V6_LP64_OFF64_LDFLAGS:
  case _CS_POSIX_V6_LP64_OFF64_LIBS:
  case _CS_POSIX_V6_LP64_OFF64_LINTFLAGS:
  case _CS_POSIX_V7_LP64_OFF64_CFLAGS:
  case _CS_POSIX_V7_LP64_OFF64_LDFLAGS:
  case _CS_POSIX_V7_LP64_OFF64_LIBS:
  case _CS_POSIX_V7_LP64_OFF64_LINTFLAGS:
    // The native LP64 model needs no additional compiler or linker flags.
    if (LP64)
      value = "";
    break;
  case _CS_V6_ENV:
  case _CS_V7_ENV:
#ifdef _CS_V8_ENV
  case _CS_V8_ENV:
#endif
    // No complete POSIX-conforming utility environment is supplied.
    break;
  default:
#ifdef _CS_POSIX_V8_ILP32_OFF32_CFLAGS
    if (name >= _CS_POSIX_V8_ILP32_OFF32_CFLAGS &&
        name <= _CS_POSIX_V8_LPBIG_OFFBIG_LIBS)
      return 0;
#endif
    // Other valid model queries have no available configuration value.
    if (!((name >= _CS_POSIX_V6_ILP32_OFF32_CFLAGS &&
           name <= _CS_POSIX_V6_LPBIG_OFFBIG_LINTFLAGS) ||
          (name >= _CS_POSIX_V7_ILP32_OFF32_CFLAGS &&
           name <= _CS_POSIX_V7_LPBIG_OFFBIG_LINTFLAGS)))
      libc_errno = EINVAL;
    return 0;
  }
  if (!value)
    return 0;
  size_t size = 0;
  while (value[size])
    ++size;
  if (len) {
    size_t count = size < len - 1 ? size : len - 1;
    for (size_t i = 0; i < count; ++i)
      buf[i] = value[i];
    buf[count] = '\0';
  }
  return size + 1;
}

} // namespace LIBC_NAMESPACE_DECL
