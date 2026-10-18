//===-- Named semaphore implementation for Linux --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/semaphore/linux/semaphore.h"

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "src/__support/CPP/array.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/CPP/mutex.h"
#include "src/__support/CPP/new.h"
#include "src/__support/CPP/scope.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/OSUtil/linux/stat/stat_via_statx.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/ftruncate.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/getrandom.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/link.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/mmap.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/munmap.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/unlink.h"
#include "src/__support/ctype_utils.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"
#include "src/__support/threads/raw_mutex.h"
#include "src/string/memory_utils/inline_memcpy.h"
#include "src/sys/mman/linux/shm_common.h"

#include <linux/mman.h> // PROT_READ, PROT_WRITE, MAP_SHARED

namespace LIBC_NAMESPACE_DECL {

namespace {

// Named semaphores are backed by files in /dev/shm/.
// a prefix "sem." is added to avoid name collision.
constexpr cpp::string_view SEM_PREFIX = "/dev/shm/sem.";

// use temporary file to solve data race and guarantee atomic publish.
// Temporary file use different prefix.
constexpr cpp::string_view SEM_TMP_PREFIX = "/dev/shm/.sem.tmp_";

// 8 random bytes from getrandom() produce a 16 character hex suffix, giving
// 2^64 possible temp names to avoid collision.
constexpr size_t RANDOM_SUFFIX_BYTES = 8;
constexpr size_t RANDOM_SUFFIX_HEX_LEN = RANDOM_SUFFIX_BYTES * 2;

// fixed-size buffer for the temp path.
using TmpPath =
    cpp::array<char, SEM_TMP_PREFIX.size() + RANDOM_SUFFIX_HEX_LEN + 1>;

// O_NOFOLLOW prevents symlink attacks to /dev/shm/. O_CLOEXEC ensures the
// fd is not leaked to child processes across exec.
constexpr int DEFAULT_OFLAGS = O_NOFOLLOW | O_CLOEXEC;

// A mapping keeps its inode alive even after unlink. Keying by file identity
// distinguishes an unlinked semaphore from a new one with the same name.
struct Mapping {
  decltype(stat::st_dev) device;
  decltype(stat::st_ino) inode;
  Semaphore *semaphore;
  size_t references;
  Mapping *next;
};
RawMutex mappings_mutex;
Mapping *mappings = nullptr;

ErrorOr<TmpPath> generate_tmp_path() {
  // fill out 8 random bytes.
  cpp::array<uint8_t, RANDOM_SUFFIX_BYTES> rand_bytes{};
  size_t filled = 0;
  while (filled != rand_bytes.size()) {
    auto ret = linux_syscalls::getrandom(rand_bytes.data() + filled,
                                         rand_bytes.size() - filled, 0);
    if (!ret) {
      if (ret.error() == EINTR)
        continue;
      return Error(ret.error());
    }
    if (ret.value() == 0)
      return Error(EIO);
    filled += static_cast<size_t>(ret.value());
  }

  TmpPath path;
  inline_memcpy(path.data(), SEM_TMP_PREFIX.data(), SEM_TMP_PREFIX.size());

  // Encode each random byte as two hex digits, and fill out tmp path.
  char *dst = path.data() + SEM_TMP_PREFIX.size();
  for (size_t i = 0; i < RANDOM_SUFFIX_BYTES; ++i) {
    *dst++ = internal::int_to_b36_char(rand_bytes[i] >> 4);
    *dst++ = internal::int_to_b36_char(rand_bytes[i] & 0xf);
  }
  *dst = '\0';
  return path;
}

// map an open semaphore fd into memory.
ErrorOr<Semaphore *> map_semaphore(int fd) {
  cpp::scope_exit close_fd([&] { linux_syscalls::close(fd); });
  struct stat status;
  auto stat_or = internal::stat_via_statx(fd, "", AT_EMPTY_PATH, &status);
  if (!stat_or)
    return Error(stat_or.error());
  if (status.st_size < static_cast<off_t>(sizeof(Semaphore)))
    return Error(EINVAL);

  cpp::lock_guard lock(mappings_mutex);
  for (Mapping *entry = mappings; entry; entry = entry->next) {
    if (entry->device == status.st_dev && entry->inode == status.st_ino) {
      if (entry->references == cpp::numeric_limits<size_t>::max())
        return Error(EOVERFLOW);
      ++entry->references;
      return entry->semaphore;
    }
  }

  auto entry_or =
      linux_syscalls::mmap(nullptr, sizeof(Mapping), PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (!entry_or)
    return Error(entry_or.error());
  auto mmap_or = linux_syscalls::mmap(
      nullptr, sizeof(Semaphore), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (!mmap_or.has_value()) {
    linux_syscalls::munmap(entry_or.value(), sizeof(Mapping));
    return Error(mmap_or.error());
  }

  auto *sem = reinterpret_cast<Semaphore *>(mmap_or.value());
  mappings = new (entry_or.value())
      Mapping{status.st_dev, status.st_ino, sem, 1, mappings};
  return sem;
}

// open an existing named semaphore file and map it.
ErrorOr<Semaphore *> open_existing(const char *path) {
  auto fd_or = linux_syscalls::open(path, O_RDWR | DEFAULT_OFLAGS, 0);
  if (!fd_or.has_value())
    return Error(fd_or.error());
  return map_semaphore(fd_or.value());
}

} // anonymous namespace

ErrorOr<Semaphore *> Semaphore::open(const char *name, int oflag, mode_t mode,
                                     unsigned int value) {
  auto path_or = shm_common::translate_name<SEM_PREFIX>(name);
  if (!path_or.has_value())
    return Error(path_or.error());

  // open an existing semaphore.
  if (!(oflag & O_CREAT))
    return open_existing(path_or->data());

  // Existing objects ignore mode and value and do not require directory write
  // permission or a temporary file.
  if (!(oflag & O_EXCL)) {
    auto existing = open_existing(path_or->data());
    if (existing || existing.error() != ENOENT)
      return existing;
  }

  // check semaphore value.
  if (value > SEM_VALUE_MAX)
    return Error(EINVAL);

  // two step creation:
  // 1. create and fully initialize a temporary file.
  // 2. link() it and publish to the final path atomically.
  // This ensures no other process can observe a partially-initialized
  // semaphore through the final path. If link() fails with EEXIST and
  // O_EXCL is not set, fall back to opening the existing semaphore.

  auto tmp_or = generate_tmp_path();
  if (!tmp_or.has_value())
    return Error(tmp_or.error());

  // A temporary-name collision is not an EEXIST failure of the public name.
  auto fd_or = linux_syscalls::open(
      tmp_or->data(), O_RDWR | O_CREAT | O_EXCL | DEFAULT_OFLAGS, mode);
  while (!fd_or && fd_or.error() == EEXIST) {
    tmp_or = generate_tmp_path();
    if (!tmp_or)
      return Error(tmp_or.error());
    fd_or = linux_syscalls::open(
        tmp_or->data(), O_RDWR | O_CREAT | O_EXCL | DEFAULT_OFLAGS, mode);
  }
  if (!fd_or.has_value())
    return Error(fd_or.error());

  int fd = fd_or.value();

  // resizing temporary semaphore backing file.
  auto trunc_or =
      linux_syscalls::ftruncate(fd, static_cast<off_t>(sizeof(Semaphore)));
  if (!trunc_or.has_value()) {
    linux_syscalls::close(fd);
    linux_syscalls::unlink(tmp_or->data());
    return Error(trunc_or.error());
  }

  // map_semaphore closes the fd.
  auto sem_or = map_semaphore(fd);
  if (!sem_or.has_value()) {
    linux_syscalls::unlink(tmp_or->data());
    return Error(sem_or.error());
  }

  Semaphore *sem = sem_or.value();

  // Named semaphores are backed by MAP_SHARED memory and used across
  // processes, so they use shared futex addressing mode.
  new (sem) Semaphore(value, /*is_shared=*/true);

  // atomically publish the fully initialized semaphore.
  auto link_or = linux_syscalls::link(tmp_or->data(), path_or->data());

  // temp file is no longer needed.
  linux_syscalls::unlink(tmp_or->data());

  // link() succees
  if (link_or.has_value())
    return sem;

  // link() fail, clean up the mapping.
  Semaphore::close(sem);

  // if the name already exists and O_EXCL was not set, open existing.
  if (link_or.error() == EEXIST && !(oflag & O_EXCL))
    return open_existing(path_or->data());

  return Error(link_or.error());
}

int Semaphore::close(Semaphore *sem) {
  cpp::lock_guard lock(mappings_mutex);
  for (Mapping **slot = &mappings; *slot; slot = &(*slot)->next) {
    Mapping *entry = *slot;
    if (entry->semaphore != sem)
      continue;
    if (entry->references > 1) {
      --entry->references;
      return 0;
    }
    auto result = linux_syscalls::munmap(sem, sizeof(Semaphore));
    if (!result)
      return result.error();
    *slot = entry->next;
    linux_syscalls::munmap(entry, sizeof(Mapping));
    return 0;
  }
  return EINVAL;
}

int Semaphore::unlink(const char *name) {
  auto path_or = shm_common::translate_name<SEM_PREFIX>(name);
  if (!path_or.has_value())
    return path_or.error();

  auto result = linux_syscalls::unlink(path_or->data());
  if (!result.has_value())
    return result.error();
  return 0;
}

} // namespace LIBC_NAMESPACE_DECL
