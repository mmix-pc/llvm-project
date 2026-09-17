//===-- MMIX Linux terminal pathname query --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/unistd/ttyname_r.h"
#include "hdr/sys_stat_macros.h"
#include "src/__support/CPP/stringstream.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/sys/stat/fstat.h"
#include "src/sys/stat/stat.h"
#include "src/unistd/isatty.h"
#include "src/unistd/readlink.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, ttyname_r, (int fd, char *buffer, size_t size)) {
  if (!LIBC_NAMESPACE::isatty(fd))
    return libc_errno == EINVAL ? ENOTTY : int(libc_errno);
  if (!size)
    return ERANGE;
  if (!buffer)
    return EINVAL;
  struct stat descriptor;
  if (LIBC_NAMESPACE::fstat(fd, &descriptor) < 0)
    return libc_errno;

  char path[sizeof("/proc/self/fd/") + IntegerToString<int>::buffer_size()];
  cpp::StringStream stream({path, sizeof(path)});
  stream << "/proc/self/fd/" << fd << '\0';
  // FIXME: Add a /dev search fallback for systems without mounted procfs.
  ssize_t length = LIBC_NAMESPACE::readlink(path, buffer, size);
  if (length < 0)
    return libc_errno;
  if (static_cast<size_t>(length) >= size)
    return ERANGE;
  buffer[length] = '\0';
  struct stat named;
  if (LIBC_NAMESPACE::stat(buffer, &named) < 0)
    return libc_errno;
  if (!S_ISCHR(named.st_mode) || !S_ISCHR(descriptor.st_mode) ||
      named.st_dev != descriptor.st_dev || named.st_ino != descriptor.st_ino ||
      named.st_rdev != descriptor.st_rdev)
    return ENODEV;
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
