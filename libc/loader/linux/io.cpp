//===-- Loader bootstrap I/O ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/io.h"
#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "loader/linux/syscall.h"
#include <linux/stat.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace loader {

static IOResult transfer(long number, int fd, long address, size_t size) {
  if (size > __LONG_MAX__ ||
      static_cast<unsigned long>(address) > __LONG_MAX__ - size)
    return {0, -EOVERFLOW};
  size_t done = 0;
  while (done < size) {
    long result = raw_syscall(number, fd, address + static_cast<long>(done),
                              static_cast<long>(size - done));
    if (result == -EINTR)
      continue;
    if (result < 0)
      return {done, result};
    if (result == 0)
      return {done, number == SYS_read ? 0 : -EIO};
    if (static_cast<unsigned long>(result) > size - done)
      return {done, -EIO};
    done += static_cast<size_t>(result);
  }
  return {done, 0};
}

IOResult read_bytes(int fd, void *buffer, size_t size) {
  return transfer(SYS_read, fd, reinterpret_cast<long>(buffer), size);
}

IOResult write_bytes(int fd, const void *buffer, size_t size) {
  return transfer(SYS_write, fd, reinterpret_cast<long>(buffer), size);
}

long OwnedFd::open_readonly(const char *path) {
  if (value >= 0)
    return -EBUSY;
  long result;
  do {
    result = raw_syscall(SYS_openat, AT_FDCWD, reinterpret_cast<long>(path),
                         O_RDONLY | O_CLOEXEC);
  } while (result == -EINTR);
  if (result < 0)
    return result;
  value = static_cast<int>(result);
  return 0;
}

long OwnedFd::close() {
  if (value < 0)
    return 0;
  int fd = value;
  value = -1;
  return raw_syscall(SYS_close, fd);
}

long OwnedFd::take(OwnedFd &from) {
  if (value >= 0 || from.value < 0)
    return -EINVAL;
  value = from.value;
  from.value = -1;
  return 0;
}

long OwnedFd::identity(FileIdentity &out) const {
  if (value < 0)
    return -EBADF;
  struct statx status;
  __builtin_memset_inline(&status, 0, sizeof(status));
  long result;
  do {
    result = raw_syscall(SYS_statx, value, reinterpret_cast<long>(""),
                         AT_EMPTY_PATH, STATX_INO | STATX_TYPE,
                         reinterpret_cast<long>(&status));
  } while (result == -EINTR);
  if (result < 0)
    return result;
  if ((status.stx_mask & (STATX_INO | STATX_TYPE)) != (STATX_INO | STATX_TYPE) ||
      (status.stx_mode & S_IFMT) != S_IFREG)
    return -ENOEXEC;
  out = {status.stx_ino, status.stx_dev_major, status.stx_dev_minor};
  return 0;
}

[[noreturn]] void fatal_startup() {
  static constexpr char message[] = "LLVM loader: startup failure\n";
  size_t done = 0;
  // A blocking diagnostic fd can still block; attempts, not wall time, are
  // bounded.
  for (unsigned attempt = 0; attempt < 8 && done < sizeof(message) - 1;
       ++attempt) {
    long result =
        raw_syscall(SYS_write, 2, reinterpret_cast<long>(message + done),
                    static_cast<long>(sizeof(message) - 1 - done));
    if (result == -EINTR)
      continue;
    if (result <= 0 ||
        static_cast<unsigned long>(result) > sizeof(message) - 1 - done)
      break;
    done += static_cast<size_t>(result);
  }
  raw_syscall(SYS_exit_group, 127);
  __builtin_trap();
}

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
