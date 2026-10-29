//===-- Linux implementation of gethostid ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/unistd/gethostid.h"

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/stdint_proxy.h"
#include "hdr/types/size_t.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/read.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(long, gethostid, ()) {
  // This file is the only source of identity; there is no hostname/DNS
  // fallback. Raw wrappers add no cancellation points and leave errno
  // unchanged. That is an implementation policy, not a standard requirement on
  // gethostid's errno.
  auto fd = linux_syscalls::open("/etc/hostid", O_RDONLY | O_CLOEXEC, 0);
  if (!fd.has_value())
    return 0; // All open errors, including EINTR, mean unconfigured.

  uint32_t hostid = 0;
  unsigned char extra;
  size_t size = 0;
  bool valid = false;
  while (true) {
    // Read the target-native object representation, then require EOF rather
    // than accepting the four-byte prefix of a longer file.
    void *buffer = size < sizeof(hostid)
                       ? reinterpret_cast<unsigned char *>(&hostid) + size
                       : &extra;
    size_t count = size < sizeof(hostid) ? sizeof(hostid) - size : 1;
    auto result = linux_syscalls::read(*fd, buffer, count);
    if (!result.has_value()) {
      // Retry interrupted reads without discarding an already-read prefix.
      // All other read errors, including during the EOF probe, fail closed.
      if (result.error() == EINTR)
        continue;
      break;
    }
    if (*result == 0) {
      valid = size == sizeof(hostid);
      break;
    }
    size += static_cast<size_t>(*result);
    if (size > sizeof(hostid))
      break;
  }

  // Close exactly once even on failure. Retrying close after EINTR could close
  // a reused descriptor on Linux. Any close failure also discards the value.
  auto closed = linux_syscalls::close(*fd);
  return valid && closed.has_value() ? static_cast<long>(hostid) : 0;
}

} // namespace LIBC_NAMESPACE_DECL
