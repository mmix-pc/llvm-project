//===-- MMIX Linux cancellable fcntl --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/fcntl/fcntl.h"

#include "src/__support/OSUtil/linux/syscall_wrappers/fcntl.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include "src/fcntl/linux/fcntl_args.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, fcntl, (int fd, int cmd, ...)) {
  va_list args;
  va_start(args, cmd);
  void *arg = linux_fcntl::read_argument(cmd, args);
  va_end(args);

  if (cmd == F_SETLKW || cmd == F_OFD_SETLKW) {
    // MMIX uses the native 64-bit flock layout for both lock families.
    // Only blocking acquisition is a cancellation point; other commands keep
    // the generic wrapper's conversions, including negative F_GETOWN results.
    long result = mmix::cancellation_syscall(SYS_fcntl, fd, cmd,
                                            reinterpret_cast<long>(arg));
    if (result < 0) {
      libc_errno = static_cast<int>(-result);
      return -1;
    }
    return static_cast<int>(result);
  }

  auto result = linux_syscalls::fcntl(fd, cmd, arg);
  if (!result) {
    libc_errno = result.error();
    return -1;
  }
  return result.value();
}

} // namespace LIBC_NAMESPACE_DECL
