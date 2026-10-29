//===-- Linux file ownership for BSD hash databases -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hash_file.h"
#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "hdr/types/off_t.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/linux/stat/stat_via_statx.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/fsync.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/alloc-checker.h"
#include "src/__support/macros/sanitizer.h"
#include "src/ndbm/hash_database.h"
#include "src/ndbm/hash_function.h"
#include "src/ndbm/hash_page.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace dbm {
namespace {
// DBM is not a cancellation point. In particular, do not call the public
// pread/pwrite leaves while publishing database metadata. This does not make
// database operations async-cancel-safe or signal-safe.
template <typename Pointer>
long positional_io(long number, int fd, Pointer bytes, size_t size,
                   uint64_t offset) {
  if constexpr (sizeof(long) == sizeof(uint32_t) &&
                sizeof(off_t) == sizeof(uint64_t))
    return syscall_impl<long>(
        number, fd, bytes, size,
        static_cast<long>(static_cast<uint32_t>(offset)),
        static_cast<long>(static_cast<uint32_t>(offset >> 32)));
  else
    return syscall_impl<long>(number, fd, bytes, size, off_t(offset));
}
} // namespace

HashHeader HashFile::empty_header() {
  HashHeader header{};
  header.byte_order = 4321;
  header.directory_size = 1;
  header.overflow_point = 1;
  header.max_bucket = header.low_mask = 1;
  header.high_mask = 3;
  header.fill_factor = 40;
  header.spares[1] = 1;
  header.bitmaps[0] = 2049;
  return header;
}

ErrorOr<size_t> HashFile::prepare_empty(HashWriteBatch &batch) {
  if (batch.size())
    return Error(EINVAL);
  unsigned char page[HASH_PAGE_SIZE];
  initialize_hash_page(page, sizeof(page), true);
  for (uint32_t primary = 1; primary <= 2; ++primary) {
    auto staged = batch.stage(primary, page);
    if (!staged)
      return Error(staged.error());
  }
  for (auto &byte : page)
    byte = 0;
  page[3] = 1; // The first allocated overflow address owns the bitmap itself.
  return batch.stage(3, page);
}

bool HashFile::readable() const { return fd >= 0 && mode != O_WRONLY; }
bool HashFile::writable() const { return fd >= 0 && mode != O_RDONLY; }

ErrorOr<int> HashFile::close() {
  int consumed = fd;
  fd = -1;
  if (consumed < 0)
    return 0;
  return linux_syscalls::close(consumed);
}

ErrorOr<HashFileState> HashFile::open(const char *path, int flags,
                                      mode_t permissions) {
  if (fd >= 0)
    return Error(EBUSY);
  constexpr int allowed = O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC | O_CLOEXEC |
                          O_SYNC | O_DSYNC | O_RSYNC;
  int access = flags & O_ACCMODE;
  if (!path || (flags & ~allowed) ||
      (access != O_RDONLY && access != O_WRONLY && access != O_RDWR) ||
      ((flags & O_TRUNC) && access == O_RDONLY) ||
      ((flags & O_EXCL) && !(flags & O_CREAT)))
    return Error(EINVAL);
  size_t length = 0;
  while (path[length]) {
    if (length == SIZE_MAX - 4)
      return Error(EOVERFLOW);
    ++length;
  }
  AllocChecker ac;
  char *name = new (ac) char[length + 4];
  if (!ac)
    return Error(ENOMEM);
  for (size_t i = 0; i < length; ++i)
    name[i] = path[i];
  name[length] = '.';
  name[length + 1] = 'd';
  name[length + 2] = 'b';
  name[length + 3] = '\0';
  int underlying =
      (flags & ~O_ACCMODE) | (access == O_WRONLY ? O_RDWR : access);
  auto opened = linux_syscalls::open(name, underlying, permissions);
  delete[] name;
  if (!opened)
    return Error(opened.error());
  fd = *opened;
  mode = access;
  sync_writes = flags & (O_SYNC | O_DSYNC);
  auto state = inspect();
  if (!state)
    close();
  return state;
}

ErrorOr<HashFileState> HashFile::inspect() {
  if (fd < 0)
    return Error(EBADF);
  struct stat status{};
  auto stat = internal::stat_via_statx(fd, "", AT_EMPTY_PATH, &status);
  if (!stat)
    return Error(stat.error());
  if (!S_ISREG(status.st_mode) || status.st_size < 0)
    return Error(EINVAL);
  uint64_t extent = status.st_size;
  if (!extent)
    return HashFileState{empty_header(), 0};
  auto header = read_hash_header(io(), hash_signature());
  if (!header)
    return Error(header.error());
  auto valid = validate_hash_database(io(), *header, extent);
  if (!valid)
    return Error(valid.error());
  return HashFileState{*header, extent};
}

ErrorOr<size_t> HashFile::read(void *context, unsigned char *bytes, size_t size,
                               uint64_t offset) {
  auto &file = *static_cast<HashFile *>(context);
  if (offset > uint64_t(cpp::numeric_limits<off_t>::max()))
    return Error(EOVERFLOW);
  auto result = positional_io(SYS_pread64, file.fd, bytes, size, offset);
  if (result < 0)
    return Error(static_cast<int>(-result));
  LIBC_MSAN_UNPOISON(bytes, size_t(result));
  return size_t(result);
}

ErrorOr<size_t> HashFile::write(void *context, const unsigned char *bytes,
                                size_t size, uint64_t offset) {
  auto &file = *static_cast<HashFile *>(context);
  if (!file.writable())
    return Error(EBADF);
  if (offset > uint64_t(cpp::numeric_limits<off_t>::max()))
    return Error(EOVERFLOW);
  auto result = positional_io(SYS_pwrite64, file.fd, bytes, size, offset);
  if (result < 0)
    return Error(static_cast<int>(-result));
  return size_t(result);
}

ErrorOr<size_t> HashFile::sync(void *context) {
  auto &file = *static_cast<HashFile *>(context);
  auto result = linux_syscalls::fsync(file.fd);
  if (!result)
    return Error(result.error());
  return size_t{0};
}

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
