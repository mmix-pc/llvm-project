//===-- MMIX Linux descriptor-based pseudo-terminal grant -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/grantpt.h"
#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/sys_ioctl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "hdr/types/gid_t.h"
#include "hdr/types/mode_t.h"
#include "hdr/types/uid_t.h"
#include "src/__support/OSUtil/linux/stat/kernel_statx_types.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/fchmodat.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/ioctl.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/statx.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#ifdef LIBC_MMIX_PTY_CANCELLATION
#include "src/__support/threads/linux/mmix/cancellation.h"
#include <pthread.h>
#endif
#include <linux/stat.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace {
class GrantptScope {
#ifdef LIBC_MMIX_PTY_CANCELLATION
  int saved_state = PTHREAD_CANCEL_DISABLE;
  bool restore = false;
#endif

public:
  int peer = -1;

  GrantptScope() {
#ifdef LIBC_MMIX_PTY_CANCELLATION
    int error = mmix::set_cancel_state(PTHREAD_CANCEL_DISABLE, &saved_state);
    // As in the accounting scope, startup may have no cancellation context.
    if (error && error != ENOTSUP)
      __builtin_trap();
    restore = !error;
#endif
  }
  GrantptScope(const GrantptScope &) = delete;
  GrantptScope &operator=(const GrantptScope &) = delete;

  int close_peer(int error) {
    if (peer < 0)
      return error;
    int fd = peer;
    peer = -1;
    // Linux consumes the descriptor even on EINTR. Never retry or replace an
    // earlier metadata error with a cleanup error.
    auto result = linux_syscalls::close(fd);
    if (error)
      return error;
    return result ? 0 : result.error();
  }

  ~GrantptScope() noexcept(false) {
    (void)close_peer(0);
#ifdef LIBC_MMIX_PTY_CANCELLATION
    // Restoring asynchronous cancellation may unwind; no peer may remain live.
    if (restore && mmix::set_cancel_state(saved_state, nullptr))
      __builtin_trap();
#endif
  }
};

int slave_metadata(int peer, internal::kernel_statx_buf &metadata) {
  constexpr unsigned MASK = STATX_TYPE | STATX_MODE | STATX_UID;
  metadata = {};
  auto result = linux_syscalls::statx(peer, "", AT_EMPTY_PATH, MASK, &metadata);
  if (!result)
    return result.error();
  if ((metadata.stx_mask & MASK) != MASK || !S_ISCHR(metadata.stx_mode))
    return EIO;
  return 0;
}

int repair_slave(int peer) {
  internal::kernel_statx_buf metadata{};
  int error = slave_metadata(peer, metadata);
  if (error)
    return error;
  uid_t owner = syscall_impl<uid_t>(SYS_getuid);
  constexpr mode_t MODE = S_IRUSR | S_IWUSR | S_IWGRP;
  // Set the mode before transferring ownership, which can remove our chmod
  // authority. Neither operation changes the PTY lock or uses a pathname.
  if ((metadata.stx_mode & 07777) != MODE) {
    auto result = linux_syscalls::fchmodat(peer, "", MODE, AT_EMPTY_PATH);
    if (!result)
      return result.error();
  }
  if (metadata.stx_uid != owner) {
    long result = syscall_impl<long>(SYS_fchownat, peer, "", owner,
                                    static_cast<gid_t>(-1), AT_EMPTY_PATH);
    if (result < 0)
      return static_cast<int>(-result);
  }
  error = slave_metadata(peer, metadata);
  if (error)
    return error;
  if (metadata.stx_uid != owner || (metadata.stx_mode & 07777) != MODE)
    return EIO;
  return 0;
}

int grant_slave(int fd) {
  GrantptScope scope;
  unsigned int index;
  auto manager = linux_syscalls::ioctl(fd, TIOCGPTN, &index);
  if (!manager)
    return manager.error() == ENOTTY ? EINVAL : manager.error();

  // Linux dentry_open(O_PATH) bypasses tty_open, including its slave-lock
  // check. TIOCGPTPEER binds the metadata handle to this master's devpts
  // instance; an index-based /dev/pts pathname cannot establish that identity.
  auto peer = linux_syscalls::ioctl(fd, TIOCGPTPEER, O_PATH | O_CLOEXEC);
  if (!peer)
    return peer.error();
  scope.peer = *peer;
  return scope.close_peer(repair_slave(scope.peer));
}
} // namespace

LLVM_LIBC_FUNCTION(int, grantpt, (int fd)) {
  int error = grant_slave(fd);
  if (error) {
    libc_errno = error;
    return -1;
  }
  return 0;
}
} // namespace LIBC_NAMESPACE_DECL
