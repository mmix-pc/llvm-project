//===-- MMIX Linux terminal pathname query --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/unistd/ttyname_r.h"
#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/limits_macros.h"
#include "hdr/sys_ioctl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "hdr/types/struct_dirent.h"
#include "src/__support/CPP/stringstream.h"
#include "src/__support/OSUtil/linux/stat/kernel_statx_types.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/ioctl.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/readlinkat.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/statx.h"
#include "src/__support/common.h"
#include "src/string/memory_utils/inline_memcpy.h"
#ifdef LIBC_MMIX_TTY_CANCELLATION
#include "src/__support/threads/linux/mmix/cancellation.h"
#include <pthread.h>
#endif
#include <linux/stat.h>

namespace LIBC_NAMESPACE_DECL {
namespace {
using Metadata = internal::kernel_statx_buf;
constexpr unsigned IDENTITY_MASK = STATX_TYPE | STATX_INO;

class NameScope {
#ifdef LIBC_MMIX_TTY_CANCELLATION
  int saved_state = PTHREAD_CANCEL_DISABLE;
  bool restore = false;
#endif
public:
  int directory = -1;
  NameScope() {
#ifdef LIBC_MMIX_TTY_CANCELLATION
    int error = mmix::set_cancel_state(PTHREAD_CANCEL_DISABLE, &saved_state);
    if (error && error != ENOTSUP)
      __builtin_trap();
    restore = !error;
#endif
  }
  NameScope(const NameScope &) = delete;
  NameScope &operator=(const NameScope &) = delete;
  int close_directory(int error) {
    if (directory < 0)
      return error;
    int fd = directory;
    directory = -1;
    // Linux consumes the descriptor even when close reports an error.
    auto result = linux_syscalls::close(fd);
    return error ? error : (result ? 0 : result.error());
  }
  ~NameScope() noexcept(false) {
    (void)close_directory(0);
#ifdef LIBC_MMIX_TTY_CANCELLATION
    if (restore && mmix::set_cancel_state(saved_state, nullptr))
      __builtin_trap();
#endif
  }
};

int verify_name(int directory, const char *path, const Metadata &descriptor) {
  Metadata named{};
  auto result =
      linux_syscalls::statx(directory, path, 0, IDENTITY_MASK, &named);
  if (!result)
    return result.error();
  if ((named.stx_mask & IDENTITY_MASK) != IDENTITY_MASK)
    return EIO;
  if (!S_ISCHR(named.stx_mode) || named.stx_ino != descriptor.stx_ino ||
      named.stx_dev_major != descriptor.stx_dev_major ||
      named.stx_dev_minor != descriptor.stx_dev_minor ||
      named.stx_rdev_major != descriptor.stx_rdev_major ||
      named.stx_rdev_minor != descriptor.stx_rdev_minor)
    return ENODEV;
  return 0;
}

int search_directory(const char *directory, const Metadata &descriptor,
                     char (&path)[PATH_MAX], size_t &length, NameScope &scope) {
  auto opened =
      linux_syscalls::open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);
  if (!opened)
    return opened.error();
  scope.directory = *opened;
  constexpr size_t NAME_OFFSET = __builtin_offsetof(struct dirent, d_name);
  constexpr size_t LENGTH_OFFSET = __builtin_offsetof(struct dirent, d_reclen);
  static_assert(NAME_OFFSET == 19 && LENGTH_OFFSET == 16,
                "Linux getdents64 wire layout required");
  unsigned char entries[4096];
  int saved_error = ENODEV;
  for (;;) {
    long count = syscall_impl<long>(SYS_getdents64, scope.directory, entries,
                                    sizeof(entries));
    if (count < 0)
      return scope.close_directory(static_cast<int>(-count));
    if (!count)
      return scope.close_directory(saved_error);
    if (static_cast<size_t>(count) > sizeof(entries))
      return scope.close_directory(EIO);
    size_t position = 0;
    while (position < static_cast<size_t>(count)) {
      size_t remaining = static_cast<size_t>(count) - position;
      if (remaining <= NAME_OFFSET)
        return scope.close_directory(EIO);
      unsigned short record_length;
      inline_memcpy(&record_length, entries + position + LENGTH_OFFSET,
                    sizeof(record_length));
      if (record_length <= NAME_OFFSET || record_length > remaining)
        return scope.close_directory(EIO);
      const char *name =
          reinterpret_cast<char *>(entries + position + NAME_OFFSET);
      size_t name_size = 0, capacity = record_length - NAME_OFFSET;
      while (name_size < capacity && name[name_size])
        ++name_size;
      if (!name_size || name_size == capacity)
        return scope.close_directory(EIO);
      position += record_length;
      if (name[0] == '.' &&
          (name_size == 1 || (name_size == 2 && name[1] == '.')))
        continue;
      for (size_t i = 0; i < name_size; ++i)
        if (name[i] == '/')
          return scope.close_directory(EIO);
      int error = verify_name(scope.directory, name, descriptor);
      if (!error) {
        cpp::StringStream stream({path, sizeof(path)});
        stream << directory << '/' << name << '\0';
        if (stream.overflow())
          return scope.close_directory(ENAMETOOLONG);
        length = stream.str().size() - 1;
        // The opened directory may have been renamed during the scan.
        error = verify_name(AT_FDCWD, path, descriptor);
        if (!error)
          return scope.close_directory(0);
      }
      if (error == EINTR)
        return scope.close_directory(error);
      if (error != ENODEV && error != ENOENT && saved_error == ENODEV)
        saved_error = error;
    }
  }
}

int terminal_name(int fd, char (&path)[PATH_MAX], size_t &length) {
  NameScope scope;
  int discipline;
  auto terminal = linux_syscalls::ioctl(fd, TIOCGETD, &discipline);
  if (!terminal)
    return terminal.error() == EINVAL ? ENOTTY : terminal.error();
  Metadata descriptor{};
  auto metadata =
      linux_syscalls::statx(fd, "", AT_EMPTY_PATH, IDENTITY_MASK, &descriptor);
  if (!metadata)
    return metadata.error();
  if ((descriptor.stx_mask & IDENTITY_MASK) != IDENTITY_MASK ||
      !S_ISCHR(descriptor.stx_mode))
    return EIO;
  char link[sizeof("/proc/self/fd/") + IntegerToString<int>::buffer_size()];
  cpp::StringStream stream({link, sizeof(link)});
  stream << "/proc/self/fd/" << fd << '\0';
  auto result = linux_syscalls::readlinkat(AT_FDCWD, link, path, sizeof(path));
  if (result && *result > 0 && static_cast<size_t>(*result) < sizeof(path) &&
      path[0] == '/') {
    length = *result;
    path[length] = '\0';
    int error = verify_name(AT_FDCWD, path, descriptor);
    if (!error || error == EINTR)
      return error;
  } else if (!result && result.error() == EINTR) {
    return EINTR;
  }
  int error = search_directory("/dev", descriptor, path, length, scope);
  if (error != ENODEV && error != ENOENT && error != ENOTDIR && error != EACCES)
    return error;
  int other = search_directory("/dev/pts", descriptor, path, length, scope);
  if (other != ENODEV && other != ENOENT && other != ENOTDIR && other != EACCES)
    return other;
  if (error != ENODEV && error != ENOENT)
    return error;
  return other == ENOENT ? ENODEV : other;
}
} // namespace

LLVM_LIBC_FUNCTION(int, ttyname_r, (int fd, char *buffer, size_t size)) {
  if (!buffer)
    return EINVAL;
  char path[PATH_MAX];
  size_t length;
  int error = terminal_name(fd, path, length);
  if (error)
    return error;
  if (size <= length)
    return ERANGE;
  // Release discovery resources and restore cancellation before publication.
  inline_memcpy(buffer, path, length + 1);
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
