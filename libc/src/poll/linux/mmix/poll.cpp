//===-- MMIX Linux poll through ppoll -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/poll/poll.h"
#include "src/poll/ppoll.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, poll, (pollfd *fds, nfds_t count, int timeout)) {
  timespec copy{};
  if (timeout >= 0) {
    copy.tv_sec = timeout / 1000;
    copy.tv_nsec = (timeout % 1000) * 1000000;
  }
  return LIBC_NAMESPACE::ppoll(fds, count, timeout >= 0 ? &copy : nullptr,
                               nullptr);
}
} // namespace LIBC_NAMESPACE_DECL
