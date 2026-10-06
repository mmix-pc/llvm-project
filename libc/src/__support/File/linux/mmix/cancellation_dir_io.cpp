//===-- MMIX Linux cancellation-safe directory leaves ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/File/dir.h"
#include "hdr/fcntl_macros.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
ErrorOr<int> platform_opendir(const char *name) {
  long result = mmix::cancellation_syscall(
      SYS_openat, AT_FDCWD, reinterpret_cast<long>(name),
      O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (result < 0)
    return Error(static_cast<int>(-result));
  return static_cast<int>(result);
}
ErrorOr<size_t> platform_fetch_dirents(int fd, cpp::span<uint8_t> buffer) {
  long result = mmix::cancellation_syscall(
      SYS_getdents64, fd, reinterpret_cast<long>(buffer.data()), buffer.size());
  if (result < 0)
    return Error(static_cast<int>(-result));
  return static_cast<size_t>(result);
}
int platform_closedir(int fd) {
  // Retirement never publishes a recoverable leaf after fd consumption.
  auto result = linux_syscalls::close(fd);
  return result ? 0 : result.error();
}
} // namespace LIBC_NAMESPACE_DECL
