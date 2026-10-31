//===-- MMIX AIO transfer adaptation ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_AIO_TRANSFER_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_AIO_TRANSFER_H

#include "aio_input.h"
#include "hdr/sys_socket_macros.h"
#include "hdr/sys_stat_macros.h"
#include "hdr/types/socklen_t.h"
#include "src/__support/OSUtil/linux/stat/kernel_statx_types.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/fstatfs.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/statx.h"
#include <linux/fs.h>
#include <linux/magic.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

struct AioTransferQueries {
  ErrorOr<int> flags(int fd) {
    return linux_syscalls::syscall_checked<int>(SYS_fcntl, fd, F_GETFL);
  }
  ErrorOr<unsigned> type(int fd) {
    internal::kernel_statx_buf status{};
    auto result =
        linux_syscalls::statx(fd, "", AT_EMPTY_PATH | AT_STATX_DONT_SYNC,
                              internal::KERNEL_STATX_TYPE_MASK, &status);
    if (!result)
      return Error(result.error());
    if (!(status.stx_mask & internal::KERNEL_STATX_TYPE_MASK))
      return Error(ENOTSUP);
    return unsigned(status.stx_mode & S_IFMT);
  }
  ErrorOr<unsigned long> filesystem(int fd) {
    struct statfs status{};
    auto result = linux_syscalls::fstatfs(fd, &status);
    if (!result)
      return Error(result.error());
    return static_cast<unsigned long>(status.f_type);
  }
  ErrorOr<int> pipe(int fd) {
    return linux_syscalls::syscall_checked<int>(SYS_fcntl, fd, F_GETPIPE_SZ);
  }
  ErrorOr<int> socket(int fd) {
    int type = 0;
    socklen_t size = sizeof(type);
    auto result = linux_syscalls::syscall_checked<int>(
        SYS_getsockopt, fd, SOL_SOCKET, SO_TYPE, &type, &size);
    if (!result)
      return Error(result.error());
    return size == sizeof(type) && type > 0 ? ErrorOr<int>(type)
                                            : ErrorOr<int>(Error(EPROTO));
  }
};

struct AioTransfer {
  uint32_t size;
  int64_t offset;
  unsigned flags;
};

// Called on the submitting thread with a retained duplicate, outside both
// service locks and under the surrounding process/cancellation guards. Queries
// can sleep or fail; none performs data I/O, seeks, or changes OFD flags. The
// original descriptor number remains the execution-order key, not this fd.
//
// Only native ramfs, shmem/tmpfs and ext regular-file operations are admitted.
// Their positions are ordinary signed offsets; actual backend availability,
// permissions, alignment and short transfers remain kernel results. The ext
// magic is shared with ext2: both ext2 and ext4 file operations were reviewed.
// Unknown filesystems and devices fail explicitly, not as proof of no seek.
// Pipe/socket queries check actual kernel file operations, not inode type
// alone.
//
// O_APPEND is captured at flags(). RWF_APPEND/NOAPPEND preserve this one bit
// through later alias F_SETFL changes; other status flags are NOT snapshotted.
// Unsupported per-request flags fail normally, with no unsafe retry fallback.
template <class Queries = AioTransferQueries>
LIBC_INLINE int normalize_aio_transfer(int retained_fd, const AioInput &input,
                                       AioTransfer &output,
                                       Queries queries = {}) {
  if (input.opcode != LIO_READ && input.opcode != LIO_WRITE)
    return EINVAL;
  AioTransfer result{};
  int error = normalize_aio_transfer_size(input.nbytes, result.size);
  if (error)
    return error;
  auto flags = queries.flags(retained_fd);
  if (!flags)
    return flags.error();
  const bool write = input.opcode == LIO_WRITE;
  int access = *flags & O_ACCMODE;
  if ((*flags & O_PATH) ||
      (access != O_RDWR && access != (write ? O_WRONLY : O_RDONLY)))
    return EBADF;
  auto type = queries.type(retained_fd);
  if (!type)
    return type.error();
  bool stream = false;
  switch (*type) {
  case S_IFREG: {
    auto filesystem = queries.filesystem(retained_fd);
    if (!filesystem)
      return filesystem.error();
    if (*filesystem != TMPFS_MAGIC && *filesystem != RAMFS_MAGIC &&
        *filesystem != EXT4_SUPER_MAGIC)
      return ENOTSUP;
    break;
  }
  case S_IFIFO: {
    auto capacity = queries.pipe(retained_fd);
    if (!capacity)
      return capacity.error();
    if (*capacity <= 0)
      return EPROTO;
    stream = true;
    break;
  }
  case S_IFSOCK: {
    auto socket_type = queries.socket(retained_fd);
    if (!socket_type)
      return socket_type.error();
    if (*socket_type <= 0)
      return EPROTO;
    stream = true;
    break;
  }
  default:
    return ENOTSUP;
  }

  const bool append = write && !stream && (*flags & O_APPEND);
  // Writes ignore offsets for append/stream operations. Reads retain the
  // separately selected Linux/glibc negative-offset rejection, even on streams.
  if (!append && !(write && stream) && input.offset < 0)
    return EINVAL;
  static_assert(sizeof(off_t) <= sizeof(int64_t));
  result.offset = stream || append ? 0 : int64_t(input.offset);
  result.flags = !write || stream ? 0 : (append ? RWF_APPEND : RWF_NOAPPEND);
  output = result;
  return 0;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
