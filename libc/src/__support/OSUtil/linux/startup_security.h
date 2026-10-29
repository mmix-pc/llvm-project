//===-- Linux startup security policy ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_STARTUP_SECURITY_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_STARTUP_SECURITY_H

#include "hdr/stdint_proxy.h"
#include "hdr/sys_auxv_macros.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace startup_security {

inline bool required(uintptr_t secure, uintptr_t uid, uintptr_t euid,
                     uintptr_t gid, uintptr_t egid) {
  return secure || uid != euid || gid != egid;
}

// Both entry parsers feed the original kernel auxv before environment consumers
// or application code run. Incomplete input must never select ordinary mode.
class Inputs {
  uintptr_t values[5]{};
  unsigned seen = 0;
  bool invalid = false;

public:
  bool observe(uintptr_t tag, uintptr_t value) {
    unsigned index;
    switch (tag) {
    case AT_SECURE:
      index = 0;
      break;
    case AT_UID:
      index = 1;
      break;
    case AT_EUID:
      index = 2;
      break;
    case AT_GID:
      index = 3;
      break;
    case AT_EGID:
      index = 4;
      break;
    default:
      return !invalid;
    }
    unsigned bit = 1U << index;
    if ((seen & bit) || (index && value > UINT32_MAX)) {
      invalid = true;
      return false;
    }
    seen |= bit;
    values[index] = value;
    return !invalid;
  }
  bool complete() const { return !invalid && seen == 31; }
  bool secure() const {
    return !complete() ||
           required(values[0], values[1], values[2], values[3], values[4]);
  }
};

inline bool dangerous(const char *entry) {
  // Explicit compatibility list shared by static CRT and dynamic loader.
  // Unknown names are not rejected merely for sharing a prefix.
  static constexpr char names[] =
      "GCONV_PATH\0GETCONF_DIR\0GLIBC_TUNABLES\0HOSTALIASES\0"
      "LD_AUDIT\0LD_BIND_NOT\0LD_BIND_NOW\0LD_DEBUG\0LD_DEBUG_OUTPUT\0"
      "LD_DYNAMIC_WEAK\0LD_LIBRARY_PATH\0LD_ORIGIN_PATH\0LD_PRELOAD\0"
      "LD_PROFILE\0LD_PROFILE_OUTPUT\0LD_SHOW_AUXV\0LD_VERBOSE\0LD_WARN\0"
      "LOCALDOMAIN\0LOCPATH\0MALLOC_ARENA_MAX\0MALLOC_ARENA_TEST\0"
      "MALLOC_MMAP_MAX_\0MALLOC_MMAP_THRESHOLD_\0MALLOC_PERTURB_\0"
      "MALLOC_TOP_PAD_\0MALLOC_TRACE\0MALLOC_TRIM_THRESHOLD_\0"
      "NIS_PATH\0NLSPATH\0RESOLV_HOST_CONF\0RES_OPTIONS\0TMPDIR\0TZDIR\0";
  for (const char *name = names; *name;) {
    uintptr_t i = 0;
    while (name[i] && entry[i] == name[i])
      ++i;
    if (!name[i] && (entry[i] == '=' || !entry[i]))
      return true;
    while (*name++) {
    }
  }
  return false;
}

// The caller has validated accessible, terminated kernel stack storage.
// Compact pointers, not strings, and move auxv with them so stack-walking
// consumers still find it directly after the environment's terminating NULL.
inline uintptr_t *sanitize(char **environment, const uintptr_t *auxv) {
  char **write = environment;
  for (char **read = environment; *read; ++read)
    if (!dangerous(*read))
      *write++ = *read;
  *write++ = nullptr;
  auto *result = reinterpret_cast<uintptr_t *>(write);
  auto *destination = result;
  do {
    uintptr_t tag = *auxv++, value = *auxv++;
    *destination++ = tag;
    *destination++ = value;
    if (tag == AT_NULL)
      break;
  } while (true);
  return result;
}

} // namespace startup_security
} // namespace LIBC_NAMESPACE_DECL
#endif
