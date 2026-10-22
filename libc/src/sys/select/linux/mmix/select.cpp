//===-- MMIX Linux select through pselect ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/sys/select/select.h"

#include "src/__support/CPP/limits.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/sys/select/pselect.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, select,
                   (int nfds, fd_set *__restrict read_set,
                    fd_set *__restrict write_set, fd_set *__restrict error_set,
                    timeval *__restrict timeout)) {
  timespec copy{};
  if (timeout) {
    if (timeout->tv_sec < 0 || timeout->tv_usec < 0) {
      libc_errno = EINVAL;
      return -1;
    }
    constexpr time_t MAX = cpp::numeric_limits<time_t>::max();
    if (MAX - timeout->tv_sec < timeout->tv_usec / 1000000) {
      copy.tv_sec = MAX;
      copy.tv_nsec = 999999999;
    } else {
      copy.tv_sec = timeout->tv_sec + timeout->tv_usec / 1000000;
      copy.tv_nsec = (timeout->tv_usec % 1000000) * 1000;
    }
  }
  return LIBC_NAMESPACE::pselect(nfds, read_set, write_set, error_set,
                                 timeout ? &copy : nullptr, nullptr);
}

} // namespace LIBC_NAMESPACE_DECL
