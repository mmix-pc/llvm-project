//===-- Linux utmp/utmpx file engine --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/utmp/utmp_engine.h"

#include "hdr/errno_macros.h"
#include "hdr/stdio_macros.h"
#include "hdr/types/struct_utmp.h"
#include "hdr/types/struct_utmpx.h"
#include "hdr/utmp_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/CPP/mutex.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/fcntl.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/ftruncate.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/lseek.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/read.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/write.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/sanitizer.h"
#include "src/__support/threads/raw_mutex.h"
#include "src/string/memory_utils/inline_memcpy.h"
#include "src/utmp/cancellation_scope.h"

#include <stddef.h>

// Tests may override the initial path at compile time; utmpname/utmpxname
// provide a copied, process-wide runtime override without environment lookup.
#ifndef LIBC_COPT_UTMP_FILE_PATH
#define LIBC_COPT_UTMP_FILE_PATH _PATH_UTMP
#endif

namespace LIBC_NAMESPACE_DECL {
namespace utmp_backend {
namespace {

// Do not silently build against a host's x86 compat32 accounting structures.
static_assert(sizeof(long) == 8 && sizeof(utmp) == 400 && sizeof(utmpx) == 400);
static_assert(alignof(utmp) == 8 && alignof(utmpx) == 8);
#define CHECK_FIELD(FIELD, OFFSET, SIZE)                                       \
  static_assert(offsetof(utmp, FIELD) == OFFSET &&                             \
                offsetof(utmpx, FIELD) == OFFSET);                             \
  static_assert(sizeof(utmp::FIELD) == SIZE && sizeof(utmpx::FIELD) == SIZE)
CHECK_FIELD(ut_type, 0, 2);
CHECK_FIELD(ut_pid, 4, 4);
CHECK_FIELD(ut_line, 8, 32);
CHECK_FIELD(ut_id, 40, 4);
CHECK_FIELD(ut_user, 44, 32);
CHECK_FIELD(ut_host, 76, 256);
CHECK_FIELD(ut_exit, 332, 4);
CHECK_FIELD(ut_session, 336, 8);
CHECK_FIELD(ut_tv, 344, 16);
CHECK_FIELD(ut_addr_v6, 360, 16);
CHECK_FIELD(__glibc_reserved, 376, 20);
#undef CHECK_FIELD
static_assert(offsetof(utmp, ut_tv.tv_usec) == 352 &&
              offsetof(utmpx, ut_tv.tv_usec) == 352);
static_assert(offsetof(utmp, ut_exit.e_exit) == 334 &&
              offsetof(utmpx, ut_exit.e_exit) == 334);

constexpr size_t PATH_CAPACITY = 4096;
constexpr off_t RECORD_SIZE = sizeof(utmp);
RawMutex mutex;
char pathname[PATH_CAPACITY] = LIBC_COPT_UTMP_FILE_PATH;
int cursor_fd = -1;
bool writable = false;
off_t next_offset = 0;
off_t current_offset = 0;
bool current_valid = false;
utmp utmp_result;
utmpx utmpx_result;

// Only internal, non-cancelling syscalls are used while owning resources.
// F_SETLK deliberately reports contention instead of waiting indefinitely
// while holding the cursor mutex. CancellationScope supplies the selected
// target's cancellation policy independently of these syscall wrappers.
class FileLock {
  int fd;
  bool held = false;

public:
  FileLock(int descriptor, short type) : fd(descriptor) {
    struct flock lock{};
    lock.l_type = type;
    lock.l_whence = SEEK_SET;
    auto result = linux_syscalls::fcntl(fd, F_SETLK, &lock);
    held = result.has_value();
    if (!held)
      libc_errno = result.error();
  }
  explicit operator bool() const { return held; }
  ~FileLock() {
    if (!held)
      return;
    struct flock lock{};
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    for (;;) {
      auto result = linux_syscalls::fcntl(fd, F_SETLK, &lock);
      if (result.has_value() || result.error() != EINTR)
        break;
    }
  }
  FileLock(const FileLock &) = delete;
  FileLock &operator=(const FileLock &) = delete;
};

bool valid_path(const char *path) {
  if (!path || !path[0]) {
    libc_errno = EINVAL;
    return false;
  }
  for (size_t i = 0; i < PATH_CAPACITY; ++i)
    if (!path[i])
      return true;
  libc_errno = ENAMETOOLONG;
  return false;
}

void close_cursor() {
  if (cursor_fd >= 0)
    linux_syscalls::close(cursor_fd);
  cursor_fd = -1;
  writable = false;
  next_offset = 0;
  current_valid = false;
}

bool open_cursor(bool writing) {
  if (cursor_fd >= 0 && (!writing || writable))
    return true;
  auto opened = linux_syscalls::open(
      pathname,
      (writing ? O_RDWR : O_RDONLY) | O_CLOEXEC | O_NONBLOCK | O_NOCTTY, 0);
  if (!opened.has_value()) {
    libc_errno = opened.error();
    return false;
  }
  // Close the read-only descriptor BEFORE taking a process-associated lock:
  // closing any descriptor for the inode would otherwise release that lock.
  if (cursor_fd >= 0)
    linux_syscalls::close(cursor_fd);
  cursor_fd = opened.value();
  writable = writing;
  // Reopening the pathname may select a different inode. Never carry search
  // offsets from the old descriptor into the newly opened database.
  next_offset = 0;
  current_offset = 0;
  current_valid = false;
  return true;
}

bool seek_to(int fd, off_t offset) {
  auto result = linux_syscalls::lseek(fd, offset, SEEK_SET);
  if (!result.has_value()) {
    libc_errno = result.error();
    return false;
  }
  return true;
}

// 1: complete record, 0: clean EOF, -1: error. A short record never advances
// the logical cursor, including when a later read of its remainder fails.
int read_record(int fd, off_t offset, utmp &record) {
  if (offset > cpp::numeric_limits<off_t>::max() - RECORD_SIZE) {
    libc_errno = EOVERFLOW;
    return -1;
  }
  if (!seek_to(fd, offset))
    return -1;
  size_t used = 0;
  auto *bytes = reinterpret_cast<unsigned char *>(&record);
  while (used < sizeof(record)) {
    auto result = linux_syscalls::read(fd, bytes + used, sizeof(record) - used);
    if (!result.has_value()) {
      if (result.error() == EINTR)
        continue;
      libc_errno = used ? EIO : result.error();
      return -1;
    }
    if (result.value() == 0) {
      if (used)
        libc_errno = EIO;
      return used ? -1 : 0;
    }
    LIBC_MSAN_UNPOISON(bytes + used, result.value());
    used += result.value();
  }
  return 1;
}

bool write_record(int fd, off_t offset, const utmp &record) {
  if (!seek_to(fd, offset))
    return false;
  size_t used = 0;
  const auto *bytes = reinterpret_cast<const unsigned char *>(&record);
  while (used < sizeof(record)) {
    auto result =
        linux_syscalls::write(fd, bytes + used, sizeof(record) - used);
    if (!result.has_value()) {
      if (result.error() == EINTR)
        continue;
      libc_errno = result.error();
      return false;
    }
    if (!result.value()) {
      libc_errno = EIO;
      return false;
    }
    used += result.value();
  }
  return true;
}

bool file_size(int fd, off_t &size) {
  auto result = linux_syscalls::lseek(fd, 0, SEEK_END);
  if (!result.has_value()) {
    libc_errno = result.error();
    return false;
  }
  size = result.value();
  if (size % RECORD_SIZE) {
    libc_errno = EIO;
    return false;
  }
  return true;
}

bool process_type(short type) {
  return type >= INIT_PROCESS && type <= DEAD_PROCESS;
}

bool valid_id(short type) {
  return (type >= RUN_LVL && type <= OLD_TIME) || process_type(type);
}

bool matches(const utmp &record, const utmp &key, Search search) {
  if (search == Search::LINE) {
    if (record.ut_type != LOGIN_PROCESS && record.ut_type != USER_PROCESS)
      return false;
    // GNU line names are bounded strings; an embedded NUL ends comparison.
    for (size_t i = 0; i < sizeof(record.ut_line); ++i) {
      if (record.ut_line[i] != key.ut_line[i])
        return false;
      if (!record.ut_line[i])
        break;
    }
    return true;
  }
  if (!process_type(key.ut_type))
    return record.ut_type == key.ut_type;
  if (!process_type(record.ut_type))
    return false;
  for (size_t i = 0; i < sizeof(record.ut_id); ++i)
    if (record.ut_id[i] != key.ut_id[i])
      return false;
  return true;
}

void *publish(Family family, const utmp &record) {
  if (family == Family::UTMP) {
    inline_memcpy(&utmp_result, &record, sizeof(record));
    return &utmp_result;
  }
  inline_memcpy(&utmpx_result, &record, sizeof(record));
  return &utmpx_result;
}

// Search includes the last returned record, then proceeds forward. Re-read
// that record under the file lock instead of trusting a stale cached value.
int find(const utmp &key, Search search, utmp &record, off_t &offset) {
  offset = current_valid ? current_offset : next_offset;
  for (;;) {
    int status = read_record(cursor_fd, offset, record);
    if (status != 1) {
      current_valid = false;
      next_offset = offset;
      return status;
    }
    current_offset = offset;
    current_valid = true;
    next_offset = offset + RECORD_SIZE;
    if (matches(record, key, search))
      return 1;
    offset = next_offset;
  }
}

} // namespace

void rewind() {
  CancellationScope cancellation;
  cpp::lock_guard guard(mutex);
  next_offset = 0;
  current_valid = false;
  open_cursor(false);
}

void end() {
  CancellationScope cancellation;
  cpp::lock_guard guard(mutex);
  close_cursor();
}

int name(const char *path) {
  CancellationScope cancellation;
  cpp::lock_guard guard(mutex);
  if (!valid_path(path))
    return -1;
  close_cursor();
  size_t i = 0;
  do {
    pathname[i] = path[i];
  } while (path[i++]);
  return 0;
}

void *get(Family family, Search search, const void *key) {
  CancellationScope cancellation;
  cpp::lock_guard guard(mutex);
  utmp query{};
  if (search != Search::NEXT) {
    if (!key) {
      libc_errno = EINVAL;
      return nullptr;
    }
    inline_memcpy(&query, key, sizeof(query));
    if (search == Search::ID && !valid_id(query.ut_type)) {
      libc_errno = EINVAL;
      return nullptr;
    }
  }
  if (!open_cursor(false))
    return nullptr;
  FileLock lock(cursor_fd, F_RDLCK);
  if (!lock)
    return nullptr;
  utmp record;
  if (search == Search::NEXT) {
    int status = read_record(cursor_fd, next_offset, record);
    current_valid = status == 1;
    if (status != 1)
      return nullptr;
    current_offset = next_offset;
    next_offset += RECORD_SIZE;
  } else {
    off_t offset;
    int status = find(query, search, record, offset);
    if (status != 1) {
      if (status == 0)
        libc_errno = ESRCH;
      return nullptr;
    }
  }
  return publish(family, record);
}

void *put(Family family, const void *entry) {
  CancellationScope cancellation;
  cpp::lock_guard guard(mutex);
  if (!entry) {
    libc_errno = EINVAL;
    return nullptr;
  }
  // The argument may point at either family's static return buffer.
  utmp record;
  inline_memcpy(&record, entry, sizeof(record));
  if (!valid_id(record.ut_type)) {
    libc_errno = EINVAL;
    return nullptr;
  }
  if (!open_cursor(true))
    return nullptr;
  FileLock lock(cursor_fd, F_WRLCK);
  if (!lock)
    return nullptr;
  off_t size;
  if (!file_size(cursor_fd, size))
    return nullptr;
  utmp previous;
  off_t offset;
  int status = find(record, Search::ID, previous, offset);
  if (status < 0)
    return nullptr;
  if (status == 0)
    offset = size;
  if (offset > cpp::numeric_limits<off_t>::max() - RECORD_SIZE) {
    libc_errno = EOVERFLOW;
    return nullptr;
  }
  if (!write_record(cursor_fd, offset, record)) {
    int error = libc_errno;
    // Best-effort rollback while still holding the whole-file write lock.
    if (status == 0)
      linux_syscalls::ftruncate(cursor_fd, size);
    else
      write_record(cursor_fd, offset, previous);
    current_valid = false;
    next_offset = offset;
    libc_errno = error;
    return nullptr;
  }
  current_offset = offset;
  current_valid = true;
  next_offset = offset + RECORD_SIZE;
  return publish(family, record);
}

void append(const char *path, const void *entry) {
  CancellationScope cancellation;
  cpp::lock_guard guard(mutex);
  if (!entry) {
    libc_errno = EINVAL;
    return;
  }
  if (!valid_path(path))
    return;
  utmp record;
  inline_memcpy(&record, entry, sizeof(record));
  // Accounting databases are administrator-owned: never create them here.
  auto opened = linux_syscalls::open(
      path, O_WRONLY | O_CLOEXEC | O_NONBLOCK | O_NOCTTY, 0);
  if (!opened.has_value()) {
    libc_errno = opened.error();
    return;
  }
  struct Descriptor {
    int fd;
    ~Descriptor() { linux_syscalls::close(fd); }
  } descriptor{opened.value()};
  FileLock lock(descriptor.fd, F_WRLCK);
  if (!lock)
    return;
  off_t size;
  if (!file_size(descriptor.fd, size))
    return;
  if (size > cpp::numeric_limits<off_t>::max() - RECORD_SIZE) {
    libc_errno = EOVERFLOW;
    return;
  }
  if (!write_record(descriptor.fd, size, record)) {
    int error = libc_errno;
    linux_syscalls::ftruncate(descriptor.fd, size);
    libc_errno = error;
  }
}

} // namespace utmp_backend
} // namespace LIBC_NAMESPACE_DECL
