//===-- MMIX Linux verified pseudo-terminal pathname ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/ptsname_r.h"
#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/limits_macros.h"
#include "hdr/sys_ioctl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "src/__support/CPP/stringstream.h"
#include "src/__support/OSUtil/linux/stat/kernel_statx_types.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/ioctl.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/readlinkat.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/statx.h"
#include "src/__support/common.h"
#include "src/string/memory_utils/inline_memcpy.h"
#ifdef LIBC_MMIX_PTY_CANCELLATION
#include "src/__support/threads/linux/mmix/cancellation.h"
#include <pthread.h>
#endif
#include <linux/stat.h>

namespace LIBC_NAMESPACE_DECL {
namespace {
class PtsnameScope {
#ifdef LIBC_MMIX_PTY_CANCELLATION
  int saved_state = PTHREAD_CANCEL_DISABLE;
  bool restore = false;
#endif

public:
  int peer = -1;

  PtsnameScope() {
#ifdef LIBC_MMIX_PTY_CANCELLATION
    int error = mmix::set_cancel_state(PTHREAD_CANCEL_DISABLE, &saved_state);
    // Match grantpt: startup may have no cancellation context yet.
    if (error && error != ENOTSUP)
      __builtin_trap();
    restore = !error;
#endif
  }
  PtsnameScope(const PtsnameScope &) = delete;
  PtsnameScope &operator=(const PtsnameScope &) = delete;

  int close_peer(int error) {
    if (peer < 0)
      return error;
    int fd = peer;
    peer = -1;
    // Linux retires the descriptor even on EINTR. Never retry close or
    // replace an earlier discovery error with a cleanup error.
    auto result = linux_syscalls::close(fd);
    if (error)
      return error;
    return result ? 0 : result.error();
  }

  ~PtsnameScope() noexcept(false) {
    (void)close_peer(0);
#ifdef LIBC_MMIX_PTY_CANCELLATION
    // Pending asynchronous cancellation may unwind here, after peer cleanup.
    if (restore && mmix::set_cancel_state(saved_state, nullptr))
      __builtin_trap();
#endif
  }
};

constexpr unsigned IDENTITY_MASK = STATX_TYPE | STATX_INO;

int verify_name(const char *path, const internal::kernel_statx_buf &peer) {
  internal::kernel_statx_buf named{};
  auto result = linux_syscalls::statx(AT_FDCWD, path, 0, IDENTITY_MASK, &named);
  if (!result)
    return result.error();
  if ((named.stx_mask & IDENTITY_MASK) != IDENTITY_MASK)
    return EIO;
  if (!S_ISCHR(named.stx_mode) || named.stx_ino != peer.stx_ino ||
      named.stx_dev_major != peer.stx_dev_major ||
      named.stx_dev_minor != peer.stx_dev_minor ||
      named.stx_rdev_major != peer.stx_rdev_major ||
      named.stx_rdev_minor != peer.stx_rdev_minor)
    return ENODEV;
  return 0;
}

int proc_name(int fd, const internal::kernel_statx_buf &peer,
              char (&path)[PATH_MAX], size_t &length) {
  char link[sizeof("/proc/self/fd/") + IntegerToString<int>::buffer_size()];
  cpp::StringStream stream({link, sizeof(link)});
  stream << "/proc/self/fd/" << fd << '\0';
  auto result = linux_syscalls::readlinkat(AT_FDCWD, link, path, sizeof(path));
  if (!result)
    return result.error();
  length = static_cast<size_t>(*result);
  if (length >= sizeof(path))
    return ENAMETOOLONG;
  if (!length || path[0] != '/')
    return ENODEV;
  path[length] = '\0';
  return verify_name(path, peer);
}

int subsidiary_name(int fd, char (&path)[PATH_MAX], size_t &length) {
  PtsnameScope scope;
  unsigned int index;
  auto manager = linux_syscalls::ioctl(fd, TIOCGPTN, &index);
  if (!manager)
    return manager.error();

  // As in grantpt, O_PATH bypasses the slave's lock without unlocking it.
  // Keep the actual peer alive while checking either candidate pathname.
  auto peer = linux_syscalls::ioctl(fd, TIOCGPTPEER, O_PATH | O_CLOEXEC);
  if (!peer)
    return peer.error();
  scope.peer = *peer;
  internal::kernel_statx_buf identity{};
  auto metadata = linux_syscalls::statx(scope.peer, "", AT_EMPTY_PATH,
                                       IDENTITY_MASK, &identity);
  if (!metadata)
    return scope.close_peer(metadata.error());
  if ((identity.stx_mask & IDENTITY_MASK) != IDENTITY_MASK ||
      !S_ISCHR(identity.stx_mode))
    return scope.close_peer(EIO);

  int error = proc_name(scope.peer, identity, path, length);
  if (error && error != EINTR) {
    // No namespace search or invented cross-namespace path. Without usable
    // procfs, accept the conventional spelling only for this exact peer.
    cpp::StringStream stream({path, sizeof(path)});
    stream << "/dev/pts/" << index << '\0';
    if (stream.overflow())
      return scope.close_peer(ENAMETOOLONG);
    length = stream.str().size() - 1;
    error = verify_name(path, identity);
  }
  return scope.close_peer(error);
}
} // namespace

LLVM_LIBC_FUNCTION(int, ptsname_r, (int fd, char *buffer, size_t size)) {
  if (!buffer)
    return EINVAL;
  char path[PATH_MAX];
  size_t length;
  int error = subsidiary_name(fd, path, length);
  if (error)
    return error;
  if (size <= length)
    return ERANGE;
  // Discovery, close and cancellation restoration precede publication.
  inline_memcpy(buffer, path, length + 1);
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
