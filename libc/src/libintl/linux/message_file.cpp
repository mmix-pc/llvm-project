//===-- Linux message object loading -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/libintl/message_file.h"
#include "hdr/fcntl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "src/__support/OSUtil/linux/stat/kernel_statx_types.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/read.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/statx.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

ErrorOr<MoCatalog *> load_message_file(const char *path) {
  auto opened = linux_syscalls::syscall_checked<int>(
      SYS_openat, AT_FDCWD, path, O_RDONLY | O_CLOEXEC | O_NONBLOCK, 0);
  if (!opened)
    return Error(opened.error());
  struct Descriptor {
    int fd;
    ~Descriptor() { linux_syscalls::close(fd); }
  } file{*opened};
  kernel_statx_buf stat{};
  constexpr unsigned TYPE_AND_SIZE = 0x001 | 0x200;
  auto status =
      linux_syscalls::statx(file.fd, "", AT_EMPTY_PATH, TYPE_AND_SIZE, &stat);
  if (!status)
    return Error(status.error());
  if ((stat.stx_mask & TYPE_AND_SIZE) != TYPE_AND_SIZE ||
      !S_ISREG(stat.stx_mode))
    return Error(EINVAL);
  if (stat.stx_size > MoCatalogView::MAX_SIZE)
    return Error(EINVAL);
  return read_message_file(
      size_t(stat.stx_size),
      [&](uint8_t *buffer, size_t size) -> ErrorOr<size_t> {
        auto result = linux_syscalls::read(file.fd, buffer, size);
        if (!result)
          return Error(result.error());
        return size_t(*result);
      });
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
