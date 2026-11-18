// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "src/unistd/daemon.h"
#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "hdr/types/struct_stat.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/sys/stat/fstat.h"
#include "src/sys/sysmacros/makedev.h"
#include "src/unistd/_exit.h"
#include "src/unistd/chdir.h"
#include "src/unistd/dup2.h"
#include "src/unistd/fork.h"
#include "src/unistd/setsid.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(int, daemon, (int nochdir, int noclose)) {
  pid_t child = LIBC_NAMESPACE::fork();
  if (child < 0)
    return -1;
  if (child > 0)
    LIBC_NAMESPACE::_exit(0);
  if (LIBC_NAMESPACE::setsid() < 0)
    return -1;
  if (!nochdir && LIBC_NAMESPACE::chdir("/") < 0)
    return -1;
  if (noclose)
    return 0;
  auto opened = linux_syscalls::open("/dev/null", O_RDWR, 0);
  if (!opened.has_value()) {
    libc_errno = opened.error();
    return -1;
  }
  int fd = opened.value();
  struct stat status;
  if (LIBC_NAMESPACE::fstat(fd, &status) < 0) {
    (void)linux_syscalls::close(fd);
    return -1;
  }
  if (!S_ISCHR(status.st_mode) ||
      status.st_rdev != LIBC_NAMESPACE::makedev(1, 3)) {
    (void)linux_syscalls::close(fd);
    libc_errno = ENODEV;
    return -1;
  }
  int result = 0;
  for (int target = 0; target < 3; ++target)
    if (LIBC_NAMESPACE::dup2(fd, target) < 0) {
      result = -1;
      break;
    }
  if (fd > 2)
    (void)linux_syscalls::close(fd);
  return result;
}
} // namespace LIBC_NAMESPACE_DECL
