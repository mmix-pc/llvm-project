//===-- Linux implementation of pathconf_utils ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <linux/limits.h>

#include "hdr/limits_macros.h"
#include "hdr/unistd_macros.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "src/unistd/linux/pathconf_utils.h"

// other linux specific includes
#include <linux/bfs_fs.h>
#include <linux/magic.h> // For common FS magics
#include <linux/minix_fs.h>

namespace LIBC_NAMESPACE_DECL {

static long unavailable_filesystem_property() {
  // FIXME: statfs does not expose these properties. Unknown metadata is not
  // an unlimited value; add rules only for filesystem-invariant properties.
  libc_errno = ENOTSUP;
  return -1;
}

static long filesizebits(const struct statfs &s) {
  switch (static_cast<unsigned long>(s.f_type)) {
#if __SIZEOF_LONG__ == 8
  // These filesystems use MAX_LFS_FILESIZE, which is LLONG_MAX on LP64.
  case RAMFS_MAGIC:
  case TMPFS_MAGIC:
  case XFS_SUPER_MAGIC:
  case BTRFS_SUPER_MAGIC:
    return 64;
#endif
  default:
    return unavailable_filesystem_property();
  }
}

static long link_max(const struct statfs &s) {
  switch (static_cast<unsigned long>(s.f_type)) {
  case MINIX_SUPER_MAGIC:
  case MINIX_SUPER_MAGIC2:
    return MINIX_LINK_MAX;
  case MINIX2_SUPER_MAGIC:
  case MINIX2_SUPER_MAGIC2:
  case MINIX3_SUPER_MAGIC:
    return MINIX2_LINK_MAX;
  case XFS_SUPER_MAGIC:
    // Linux XFS sets s_max_links to XFS_MAXLINK.
    return 0x7fffffff;
  default:
    return unavailable_filesystem_property();
  }
}

static long symlinks(const struct statfs &s) {
  switch (static_cast<unsigned long>(s.f_type)) {
  case RAMFS_MAGIC:
  case TMPFS_MAGIC:
  case EXT2_SUPER_MAGIC: // ext2 and ext4 both provide symlink operations.
  case BTRFS_SUPER_MAGIC:
  case XFS_SUPER_MAGIC:
  case MINIX_SUPER_MAGIC:
  case MINIX_SUPER_MAGIC2:
  case MINIX2_SUPER_MAGIC:
  case MINIX2_SUPER_MAGIC2:
  case MINIX3_SUPER_MAGIC:
    return 1;
  case BFS_MAGIC:
  case MSDOS_SUPER_MAGIC:
    return 0;
  default:
    return unavailable_filesystem_property();
  }
}

long checked_timestamp_resolution(unsigned long long nanoseconds) {
  if (nanoseconds > static_cast<unsigned long long>(LONG_MAX)) {
    libc_errno = EOVERFLOW;
    return -1;
  }
  return static_cast<long>(nanoseconds);
}

#ifdef _PC_TIMESTAMP_RESOLUTION
static long timestamp_resolution(const struct statfs &s) {
  switch (static_cast<unsigned long>(s.f_type)) {
  case RAMFS_MAGIC:
  case TMPFS_MAGIC:
  case BTRFS_SUPER_MAGIC:
  case XFS_SUPER_MAGIC:
    return checked_timestamp_resolution(1);
  case MINIX_SUPER_MAGIC:
  case MINIX_SUPER_MAGIC2:
  case MINIX2_SUPER_MAGIC:
  case MINIX2_SUPER_MAGIC2:
  case MINIX3_SUPER_MAGIC:
    return checked_timestamp_resolution(1000000000);
  default:
    // FIXME: Linux does not expose timestamp granularity through statfs.
    // ext4 inode formats, FUSE servers and overlay backing stores can differ;
    // do not infer their resolution from the filesystem magic alone.
    libc_errno = ENOTSUP;
    return -1;
  }
}
#endif

long pathconfig(const struct statfs &s, int name) {
  switch (name) {
  case _PC_LINK_MAX:
    return link_max(s);

  case _PC_FILESIZEBITS:
    return filesizebits(s);

  case _PC_2_SYMLINKS:
    return symlinks(s);

  case _PC_REC_MIN_XFER_SIZE:
    return s.f_bsize;

  case _PC_ALLOC_SIZE_MIN:
  case _PC_REC_XFER_ALIGN:
    return s.f_frsize;

  case _PC_MAX_CANON:
    return MAX_CANON;

  case _PC_MAX_INPUT:
    return MAX_INPUT;

  case _PC_NAME_MAX:
    return s.f_namelen;

  case _PC_PATH_MAX:
    return PATH_MAX;

  case _PC_PIPE_BUF:
    return PIPE_BUF;

  case _PC_CHOWN_RESTRICTED:
    return _POSIX_CHOWN_RESTRICTED;

  case _PC_NO_TRUNC:
    return _POSIX_NO_TRUNC;

  case _PC_VDISABLE:
    return _POSIX_VDISABLE;

#ifdef _PC_TIMESTAMP_RESOLUTION
  case _PC_TIMESTAMP_RESOLUTION:
    return timestamp_resolution(s);
#endif

#ifdef _PC_FALLOC
  case _PC_FALLOC:
    // The entrypoints query the object, not just its filesystem magic.
    return unavailable_filesystem_property();
#endif
#ifdef _PC_TEXTDOMAIN_MAX
  case _PC_TEXTDOMAIN_MAX:
    // FIXME: Establish the catalog filename limit with the gettext provider.
    libc_errno = ENOTSUP;
    return -1;
#endif

  case _PC_ASYNC_IO:
  case _PC_PRIO_IO:
  case _PC_REC_INCR_XFER_SIZE:
  case _PC_REC_MAX_XFER_SIZE:
  case _PC_SYMLINK_MAX:
  case _PC_SOCK_MAXBUF:
  case _PC_SYNC_IO:
    return -1;

  default:
    libc_errno = EINVAL;
    return -1;
  }
}

} // namespace LIBC_NAMESPACE_DECL
