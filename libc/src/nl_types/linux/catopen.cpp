//===-- Linux numbered message catalog opening ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/nl_types/catopen.h"
#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/func/free.h"
#include "hdr/sys_stat_macros.h"
#include "include/llvm-libc-macros/nl_types-macros.h"
#include "src/__support/OSUtil/linux/secure_execution.h"
#include "src/__support/OSUtil/linux/stat/kernel_statx_types.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/read.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/statx.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/locale/locale.h"
#include "src/nl_types/catalog_path.h"
#include "src/stdlib/environ_internal.h"

#ifndef LIBC_CATALOG_DEFAULT_PATH
#define LIBC_CATALOG_DEFAULT_PATH                                              \
  "/usr/share/locale/%L/%N:/usr/share/locale/%L/LC_MESSAGES/%N"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace {
ErrorOr<internal::MessageCatalog *> open_catalog(cpp::string_view path) {
  // Explicit paths and expanded candidates are both NUL-terminated by callers.
  auto opened = linux_syscalls::syscall_checked<int>(
      SYS_openat, AT_FDCWD, path.data(), O_RDONLY | O_CLOEXEC | O_NONBLOCK, 0);
  if (!opened)
    return Error(opened.error());
  struct Descriptor {
    int fd;
    ~Descriptor() { linux_syscalls::close(fd); }
  } file{*opened};
  internal::kernel_statx_buf stat{};
  constexpr unsigned TYPE_AND_SIZE = 0x001 | 0x200;
  auto status =
      linux_syscalls::statx(file.fd, "", AT_EMPTY_PATH, TYPE_AND_SIZE, &stat);
  if (!status)
    return Error(status.error());
  if ((stat.stx_mask & TYPE_AND_SIZE) != TYPE_AND_SIZE ||
      !S_ISREG(stat.stx_mode) || stat.stx_size < 12 ||
      stat.stx_size > internal::MessageCatalog::MAX_SIZE)
    return Error(ENOENT);
  const size_t size = size_t(stat.stx_size);
  AllocChecker ac;
  struct Buffer {
    uint8_t *data;
    ~Buffer() { ::free(data); }
  } buffer{new (ac) uint8_t[size]};
  if (!ac)
    return Error(ENOMEM);
  size_t offset = 0;
  while (offset != size) {
    auto count =
        linux_syscalls::read(file.fd, buffer.data + offset, size - offset);
    if (!count)
      return Error(count.error());
    if (!*count)
      return Error(EIO);
    offset += size_t(*count);
  }
  uint8_t extra;
  auto end = linux_syscalls::read(file.fd, &extra, 1);
  if (!end)
    return Error(end.error());
  if (*end)
    return Error(EIO);
  auto catalog = internal::MessageCatalog::copy({buffer.data, size});
  if (!catalog && catalog.error() == EINVAL)
    return Error(ENOENT);
  return catalog;
}
} // namespace

LLVM_LIBC_FUNCTION(nl_catd, catopen, (const char *name, int flag)) {
  if (!name || (flag != 0 && flag != NL_CAT_LOCALE)) {
    libc_errno = EINVAL;
    return reinterpret_cast<nl_catd>(-1);
  }
  int saved_errno = libc_errno;
  // Fail closed when startup security metadata is unavailable. Explicit paths
  // remain caller-selected; environment search is only for unprivileged use.
  bool environment = !is_secure_execution();
  const char *locale = "C";
  if (flag == NL_CAT_LOCALE)
    locale = internal::locale_snapshot(internal::thread_locale)
                 .__data[LC_MESSAGES]
                 ->name;
  else if (environment) {
    const char *lang = internal::EnvironmentManager::get_instance().get("LANG");
    if (lang && *lang)
      locale = lang;
  }
  const char *paths =
      environment ? internal::EnvironmentManager::get_instance().get("NLSPATH")
                  : nullptr;
  auto catalog = internal::search_catalog(
      name, locale, paths, LIBC_CATALOG_DEFAULT_PATH, open_catalog);
  if (!catalog) {
    libc_errno = catalog.error();
    return reinterpret_cast<nl_catd>(-1);
  }
  libc_errno = saved_errno;
  return *catalog;
}

} // namespace LIBC_NAMESPACE_DECL
