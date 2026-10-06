//===-- Linux stream I/O leaves -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "file.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/read.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/write.h"

namespace LIBC_NAMESPACE_DECL {
FileIOResult linux_file_write(File *f, const void *data, size_t size) {
  auto *lf = reinterpret_cast<LinuxFile *>(f);
  auto ret = linux_syscalls::write(lf->get_fd(), data, size);
  if (!ret)
    return {0, ret.error()};
  return static_cast<size_t>(ret.value());
}
FileIOResult linux_file_read(File *f, void *buf, size_t size) {
  auto *lf = reinterpret_cast<LinuxFile *>(f);
  auto ret = linux_syscalls::read(lf->get_fd(), buf, size);
  if (!ret)
    return {0, ret.error()};
  return static_cast<size_t>(ret.value());
}
ErrorOr<int> linux_file_open(const char *path, int flags, mode_t mode) {
  return linux_syscalls::open(path, flags, mode);
}
} // namespace LIBC_NAMESPACE_DECL
