//===-- MMIX Linux cancellation-safe stream leaves ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/File/linux/file.h"
#include "hdr/fcntl_macros.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
FileIOResult linux_file_write(File *f, const void *data, size_t size) {
  long result = mmix::cancellation_syscall(
      SYS_write, reinterpret_cast<LinuxFile *>(f)->get_fd(),
      reinterpret_cast<long>(data), size);
  return result < 0 ? FileIOResult(0, static_cast<int>(-result))
                    : FileIOResult(static_cast<size_t>(result));
}
FileIOResult linux_file_read(File *f, void *buf, size_t size) {
  long result = mmix::cancellation_syscall(
      SYS_read, reinterpret_cast<LinuxFile *>(f)->get_fd(),
      reinterpret_cast<long>(buf), size);
  return result < 0 ? FileIOResult(0, static_cast<int>(-result))
                    : FileIOResult(static_cast<size_t>(result));
}
ErrorOr<int> linux_file_open(const char *path, int flags, mode_t mode) {
  long result = mmix::cancellation_syscall(
      SYS_openat, AT_FDCWD, reinterpret_cast<long>(path), flags, mode);
  if (result < 0)
    return Error(static_cast<int>(-result));
  // No later cancellation point exists before the caller publishes storage
  // or closes this descriptor on allocation failure. Completed open is final.
  return static_cast<int>(result);
}
} // namespace LIBC_NAMESPACE_DECL
