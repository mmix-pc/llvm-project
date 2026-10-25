//===-- Linux locale file transport ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/locale_file.h"
#include "hdr/fcntl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "src/__support/OSUtil/linux/stat/kernel_statx_types.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/read.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/statx.h"

#ifndef LIBC_LOCALE_DATA_ROOT
#define LIBC_LOCALE_DATA_ROOT "/usr/share/llvm-libc/locale"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace internal {
namespace {
struct Descriptor {
  int fd;
  ~Descriptor() { linux_syscalls::close(fd); }
};

ErrorOr<int> open_at(int parent, const char *path, int flags) {
  return linux_syscalls::syscall_checked<int>(
      SYS_openat, parent, path, flags | O_CLOEXEC | O_NOFOLLOW, 0);
}
} // namespace

ErrorOr<LocaleDataFile *> load_locale_file_at(int root_fd,
                                              cpp::string_view name) {
  const char *selected = locale_file_name(name);
  if (!selected)
    return Error(ENOENT);
  auto directory = open_at(root_fd, selected, O_RDONLY | O_DIRECTORY);
  if (!directory)
    return Error(directory.error());
  Descriptor parent{*directory};
  auto opened = open_at(parent.fd, "locale.dat", O_RDONLY | O_NONBLOCK);
  if (!opened)
    return Error(opened.error());
  Descriptor file{*opened};
  kernel_statx_buf stat{};
  constexpr unsigned TYPE_AND_SIZE = 0x001 | 0x200;
  auto status =
      linux_syscalls::statx(file.fd, "", AT_EMPTY_PATH, TYPE_AND_SIZE, &stat);
  if (!status)
    return Error(status.error());
  if ((stat.stx_mask & TYPE_AND_SIZE) != TYPE_AND_SIZE ||
      !S_ISREG(stat.stx_mode) || stat.stx_size > LocaleDataView::MAX_SIZE)
    return Error(EINVAL);
  return read_locale_file(size_t(stat.stx_size), selected,
                          [&](uint8_t *buffer, size_t size) -> ErrorOr<size_t> {
                            auto count =
                                linux_syscalls::read(file.fd, buffer, size);
                            if (!count)
                              return Error(count.error());
                            return size_t(*count);
                          });
}

ErrorOr<LocaleDataFile *> load_locale_file(cpp::string_view name) {
  if (!locale_file_name(name))
    return Error(ENOENT);
  auto root = open_at(AT_FDCWD, LIBC_LOCALE_DATA_ROOT, O_RDONLY | O_DIRECTORY);
  if (!root)
    return Error(root.error());
  Descriptor directory{*root};
  return load_locale_file_at(directory.fd, name);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
