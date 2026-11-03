//===-- MMIX ttyname syscall-boundary regression --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/limits_macros.h"
#include "hdr/sys_ioctl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "src/__support/OSUtil/linux/stat/kernel_statx_types.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation.h"
#include "test/UnitTest/Test.h"
#include <linux/stat.h>
#include <pthread.h>
#include <sys/syscall.h>

// Native transport injection only; namespace reachability needs a guest test.
namespace {
using Metadata = LIBC_NAMESPACE::internal::kernel_statx_buf;
constexpr unsigned MASK = STATX_TYPE | STATX_INO;
constexpr const char *PROC_NAME = "/isolated/devpts/7";
constexpr const char *DIRECTORIES[] = {"/dev", "/dev/pts"};
constexpr const char *NAMES[] = {"/dev/7", "/dev/pts/7"};
constexpr size_t NAME_OFFSET = 19, LENGTH_OFFSET = 16;

struct Cancelled {};

struct Directory {
  int fd = 0;
  int open_error = 0, read_error = 0, close_error = 0;
  int relative_error = 0, absolute_error = 0;
  int opens = 0, reads = 0, closes = 0, relative_stats = 0, absolute_stats = 0;
  bool live = false;
  unsigned char entries[256]{};
  size_t count = 0;
  long reported_count = -1;
  Metadata relative{}, absolute{};
};

struct State {
  int fd = 41;
  const char *link_name = PROC_NAME;
  int ioctl_error = 0, descriptor_error = 0, readlink_error = 0, proc_error = 0;
  int ioctls = 0, descriptor_stats = 0, readlinks = 0, proc_stats = 0;
  int cancel_calls = 0, cancel_state = PTHREAD_CANCEL_ENABLE;
  bool no_cancel_context = false, cancel_on_restore = false;
  Metadata descriptor{}, proc{};
  Directory directories[2];
} state;

void append_record(Directory &directory, const char *name) {
  size_t size = __builtin_strlen(name) + 1;
  size_t padded_length = (NAME_OFFSET + size + 7) & ~size_t(7);
  ASSERT_LE(padded_length, size_t(USHRT_MAX));
  unsigned short length = static_cast<unsigned short>(padded_length);
  ASSERT_LE(directory.count + length, sizeof(directory.entries));
  unsigned char *record = directory.entries + directory.count;
  __builtin_memcpy(record + LENGTH_OFFSET, &length, sizeof(length));
  __builtin_memcpy(record + NAME_OFFSET, name, size);
  directory.count += length;
}

void reset() {
  state = State{};
  state.descriptor.stx_mask = MASK;
  state.descriptor.stx_mode = S_IFCHR | 0620;
  state.descriptor.stx_ino = 19;
  state.descriptor.stx_dev_major = 0;
  state.descriptor.stx_dev_minor = 23;
  state.descriptor.stx_rdev_major = 136;
  state.descriptor.stx_rdev_minor = 7;
  state.proc = state.descriptor;
  state.directories[0].fd = 0;
  state.directories[1].fd = 63;
  for (Directory &directory : state.directories)
    directory.relative = directory.absolute = state.descriptor;
  append_record(state.directories[1], "7");
  LIBC_NAMESPACE::libc_errno = EDOM;
}

void expect_no_live_directory() {
  for (const Directory &directory : state.directories)
    EXPECT_FALSE(directory.live);
}

long dispatch(long number, long a1 = 0, long a2 = 0, long a3 = 0, long a4 = 0,
              long a5 = 0, long = 0) {
  EXPECT_TRUE(state.no_cancel_context ||
              state.cancel_state == PTHREAD_CANCEL_DISABLE);
  if (number == SYS_ioctl) {
    ++state.ioctls;
    expect_no_live_directory();
    EXPECT_EQ(a1, long(state.fd));
    EXPECT_EQ(a2, long(TIOCGETD));
    if (state.ioctl_error)
      return -state.ioctl_error;
    *reinterpret_cast<int *>(a3) = 0;
    return 0;
  }
  if (number == SYS_statx) {
    EXPECT_EQ(a4, long(MASK));
    Metadata *result = reinterpret_cast<Metadata *>(a5);
    const char *path = reinterpret_cast<const char *>(a2);
    if (a3 == AT_EMPTY_PATH) {
      ++state.descriptor_stats;
      expect_no_live_directory();
      EXPECT_EQ(a1, long(state.fd));
      EXPECT_STREQ(path, "");
      *result = state.descriptor;
      return -state.descriptor_error;
    }
    EXPECT_EQ(a3, 0L);
    if (a1 == AT_FDCWD && __builtin_strcmp(path, state.link_name) == 0) {
      ++state.proc_stats;
      expect_no_live_directory();
      *result = state.proc;
      return -state.proc_error;
    }
    for (int i = 0; i != 2; ++i) {
      Directory &directory = state.directories[i];
      if (a1 == directory.fd) {
        EXPECT_TRUE(directory.live);
        EXPECT_STREQ(path, "7");
        ++directory.relative_stats;
        *result = directory.relative;
        return -directory.relative_error;
      }
      if (a1 == AT_FDCWD && __builtin_strcmp(path, NAMES[i]) == 0) {
        EXPECT_TRUE(directory.live);
        ++directory.absolute_stats;
        *result = directory.absolute;
        return -directory.absolute_error;
      }
    }
  }
  if (number == SYS_readlinkat) {
    ++state.readlinks;
    expect_no_live_directory();
    EXPECT_EQ(a1, long(AT_FDCWD));
    EXPECT_STREQ(reinterpret_cast<const char *>(a2),
                 state.fd == 0         ? "/proc/self/fd/0"
                 : state.fd == INT_MAX ? "/proc/self/fd/2147483647"
                                       : "/proc/self/fd/41");
    EXPECT_EQ(a4, long(PATH_MAX));
    if (state.readlink_error)
      return -state.readlink_error;
    size_t length = __builtin_strlen(state.link_name);
    if (length > static_cast<size_t>(a4))
      length = static_cast<size_t>(a4);
    __builtin_memcpy(reinterpret_cast<void *>(a3), state.link_name, length);
    return static_cast<long>(length);
  }
#ifdef SYS_openat
  if (number == SYS_openat) {
    EXPECT_EQ(a1, long(AT_FDCWD));
    const char *path = reinterpret_cast<const char *>(a2);
    EXPECT_EQ(a3, long(O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    EXPECT_EQ(a4, 0L);
#else
  if (number == SYS_open) {
    const char *path = reinterpret_cast<const char *>(a1);
    EXPECT_EQ(a2, long(O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    EXPECT_EQ(a3, 0L);
#endif
    expect_no_live_directory();
    for (int i = 0; i != 2; ++i) {
      if (__builtin_strcmp(path, DIRECTORIES[i]) != 0)
        continue;
      Directory &directory = state.directories[i];
      ++directory.opens;
      EXPECT_EQ(directory.opens, 1);
      if (i == 1)
        EXPECT_EQ(state.directories[0].opens, 1);
      if (directory.open_error)
        return -directory.open_error;
      directory.live = true;
      return directory.fd;
    }
  }
  if (number == SYS_getdents64 || number == SYS_close) {
    for (Directory &directory : state.directories) {
      if (a1 != directory.fd)
        continue;
      EXPECT_TRUE(directory.live);
      if (number == SYS_close) {
        ++directory.closes;
        EXPECT_EQ(directory.closes, 1);
        directory.live = false;
        return -directory.close_error;
      }
      ++directory.reads;
      EXPECT_EQ(a3, 4096L);
      if (directory.read_error)
        return -directory.read_error;
      if (directory.reads != 1)
        return 0;
      __builtin_memcpy(reinterpret_cast<void *>(a2), directory.entries,
                       directory.count);
      return directory.reported_count >= 0 ? directory.reported_count
                                           : long(directory.count);
    }
  }
  EXPECT_TRUE(false);
  return -ENOSYS;
}

void expect_retired(int dev_closes = 0, int pts_closes = 0) {
  expect_no_live_directory();
  EXPECT_EQ(state.directories[0].closes, dev_closes);
  EXPECT_EQ(state.directories[1].closes, pts_closes);
  EXPECT_EQ(state.cancel_calls, state.no_cancel_context ? 1 : 2);
  EXPECT_EQ(state.cancel_state, int(PTHREAD_CANCEL_ENABLE));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
}

void expect_no_scan() {
  for (const Directory &directory : state.directories) {
    EXPECT_EQ(directory.opens, 0);
    EXPECT_EQ(directory.reads, 0);
    EXPECT_EQ(directory.relative_stats, 0);
    EXPECT_EQ(directory.absolute_stats, 0);
  }
}

struct Buffer {
  char bytes[128];
  Buffer() {
    for (char &byte : bytes)
      byte = '#';
  }
  char *data() { return bytes + 1; }
  size_t capacity() const { return sizeof(bytes) - 2; }
  void unchanged() const {
    for (char byte : bytes)
      EXPECT_EQ(byte, '#');
  }
  void contains(const char *expected) const {
    size_t length = __builtin_strlen(expected);
    ASSERT_LT(length, capacity());
    EXPECT_EQ(bytes[0], '#');
    for (size_t i = 0; i <= length; ++i)
      EXPECT_EQ(bytes[i + 1], expected[i]);
    for (size_t i = length + 2; i != sizeof(bytes); ++i)
      EXPECT_EQ(bytes[i], '#');
  }
};

void mismatch(Metadata &metadata, int field) {
  switch (field) {
  case 0:
    metadata.stx_mode = S_IFREG | 0600;
    break;
  case 1:
    ++metadata.stx_ino;
    break;
  case 2:
    ++metadata.stx_dev_major;
    break;
  case 3:
    ++metadata.stx_dev_minor;
    break;
  case 4:
    ++metadata.stx_rdev_major;
    break;
  case 5:
    ++metadata.stx_rdev_minor;
    break;
  }
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
template <typename T, typename... Args>
T tty_test_syscall(long number, Args... args) {
  return static_cast<T>(dispatch(number, (long)args...));
}
namespace tty_test_syscalls {
template <typename T, typename... Args>
ErrorOr<T> tty_test_checked(long number, Args... args) {
  long result = dispatch(number, (long)args...);
  return result < 0 ? ErrorOr<T>(Error(int(-result)))
                    : ErrorOr<T>(static_cast<T>(result));
}
} // namespace tty_test_syscalls
namespace mmix {
int tty_test_set_cancel_state(int requested, int *saved) {
  ++state.cancel_calls;
  expect_no_live_directory();
  if (state.no_cancel_context)
    return ENOTSUP;
  if (saved)
    *saved = state.cancel_state;
  state.cancel_state = requested;
  if (requested == PTHREAD_CANCEL_ENABLE && state.cancel_on_restore)
    throw Cancelled{};
  return 0;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#ifndef LIBC_MMIX_TTY_CANCELLATION
#define LIBC_MMIX_TTY_CANCELLATION
#endif
#define linux_syscalls tty_test_syscalls
#define syscall_impl tty_test_syscall
#define syscall_checked tty_test_checked
#define set_cancel_state tty_test_set_cancel_state
#define ttyname_r tty_boundary_ttyname_r
#include "src/unistd/linux/mmix/ttyname_r.cpp"
#undef ttyname_r
#undef set_cancel_state
#undef syscall_checked
#undef syscall_impl
#undef linux_syscalls

namespace {
int query(Buffer &buffer) {
  return LIBC_NAMESPACE::tty_boundary_ttyname_r(state.fd, buffer.data(),
                                                buffer.capacity());
}
} // namespace

TEST(LlvmLibcMMIXTtynameSyscallTest, VerifiedProcNameDoesNotScanOrCloseCaller) {
  const int descriptors[] = {0, 41, INT_MAX};
  for (int fd : descriptors) {
    reset();
    state.fd = fd;
    Buffer buffer;
    EXPECT_EQ(query(buffer), 0);
    buffer.contains(PROC_NAME);
    EXPECT_EQ(state.ioctls, 1);
    EXPECT_EQ(state.descriptor_stats, 1);
    EXPECT_EQ(state.readlinks, 1);
    EXPECT_EQ(state.proc_stats, 1);
    expect_no_scan();
    expect_retired();
  }
}

TEST(LlvmLibcMMIXTtynameSyscallTest,
     MissingProcSearchesDevThenPtsAndClosesFdZero) {
  for (int missing_link = 0; missing_link != 2; ++missing_link) {
    reset();
    if (missing_link)
      state.readlink_error = ENOENT;
    else
      state.proc_error = ENOENT;
    // Dot entries must not be stat'ed or treated as malformed records.
    append_record(state.directories[0], ".");
    append_record(state.directories[0], "..");
    Buffer buffer;
    EXPECT_EQ(query(buffer), 0);
    buffer.contains(NAMES[1]);
    EXPECT_EQ(state.proc_stats, missing_link ? 0 : 1);
    EXPECT_EQ(state.directories[0].reads, 2);
    EXPECT_EQ(state.directories[0].relative_stats, 0);
    EXPECT_EQ(state.directories[1].reads, 1);
    EXPECT_EQ(state.directories[1].relative_stats, 1);
    EXPECT_EQ(state.directories[1].absolute_stats, 1);
    expect_retired(1, 1);
  }
}

TEST(LlvmLibcMMIXTtynameSyscallTest, DevMatchStopsBeforePts) {
  reset();
  state.readlink_error = ENOENT;
  append_record(state.directories[0], "7");
  Buffer buffer;
  EXPECT_EQ(query(buffer), 0);
  buffer.contains(NAMES[0]);
  EXPECT_EQ(state.directories[0].relative_stats, 1);
  EXPECT_EQ(state.directories[0].absolute_stats, 1);
  EXPECT_EQ(state.directories[1].opens, 0);
  expect_retired(1);
}

TEST(LlvmLibcMMIXTtynameSyscallTest, IdentityMismatchRejectsEvenSamePtyNumber) {
  for (int field = 0; field != 6; ++field) {
    for (int absolute = 0; absolute != 2; ++absolute) {
      reset();
      mismatch(state.proc, field);
      Directory &pts = state.directories[1];
      mismatch(absolute ? pts.absolute : pts.relative, field);
      Buffer buffer;
      EXPECT_EQ(query(buffer), ENODEV);
      buffer.unchanged();
      EXPECT_EQ(state.proc_stats, 1);
      EXPECT_EQ(pts.relative_stats, 1);
      EXPECT_EQ(pts.absolute_stats, absolute ? 1 : 0);
      EXPECT_EQ(pts.reads, 2);
      expect_retired(1, 1);
    }
  }
}

TEST(LlvmLibcMMIXTtynameSyscallTest, DirectTerminalErrorsStopDiscovery) {
  const int errors[] = {EBADF, ENOTTY, EINVAL, EINTR};
  for (int error : errors) {
    reset();
    state.fd = error == EBADF ? -1 : 41;
    state.ioctl_error = error;
    Buffer buffer;
    EXPECT_EQ(query(buffer), error == EINVAL ? ENOTTY : error);
    buffer.unchanged();
    EXPECT_EQ(state.ioctls, 1);
    EXPECT_EQ(state.descriptor_stats, 0);
    EXPECT_EQ(state.readlinks, 0);
    expect_no_scan();
    expect_retired();
  }
}

TEST(LlvmLibcMMIXTtynameSyscallTest,
     InterruptedProcDiscoveryDoesNotRetryOrScan) {
  int State::*failures[] = {&State::descriptor_error, &State::readlink_error,
                            &State::proc_error};
  for (auto failure : failures) {
    reset();
    state.*failure = EINTR;
    Buffer buffer;
    EXPECT_EQ(query(buffer), EINTR);
    buffer.unchanged();
    EXPECT_EQ(state.descriptor_stats, 1);
    EXPECT_EQ(state.readlinks, failure == &State::descriptor_error ? 0 : 1);
    EXPECT_EQ(state.proc_stats, failure == &State::proc_error ? 1 : 0);
    expect_no_scan();
    expect_retired();
  }
}

TEST(LlvmLibcMMIXTtynameSyscallTest, InvalidDescriptorMetadataStopsDiscovery) {
  for (int defect = 0; defect != 3; ++defect) {
    reset();
    if (defect == 0)
      state.descriptor.stx_mode = S_IFREG | 0600;
    else
      state.descriptor.stx_mask &= ~(defect == 1 ? STATX_TYPE : STATX_INO);
    Buffer buffer;
    EXPECT_EQ(query(buffer), EIO);
    buffer.unchanged();
    EXPECT_EQ(state.readlinks, 0);
    expect_no_scan();
    expect_retired();
  }
}

TEST(LlvmLibcMMIXTtynameSyscallTest, IncompleteNamedMetadataIsNeverPublished) {
  const unsigned fields[] = {STATX_TYPE, STATX_INO};
  for (unsigned field : fields) {
    for (int stage = 0; stage != 3; ++stage) {
      reset();
      state.proc.stx_mask &= ~field;
      Directory &dev = state.directories[0];
      if (stage) {
        append_record(dev, "7");
        (stage == 1 ? dev.relative : dev.absolute).stx_mask &= ~field;
      }
      Buffer buffer;
      EXPECT_EQ(query(buffer), stage ? EIO : 0);
      if (stage)
        buffer.unchanged();
      else
        buffer.contains(NAMES[1]);
      EXPECT_EQ(state.directories[1].opens, stage ? 0 : 1);
      EXPECT_EQ(dev.absolute_stats, stage == 2 ? 1 : 0);
      expect_retired(1, stage ? 0 : 1);
    }
  }
}

TEST(LlvmLibcMMIXTtynameSyscallTest, OnlySearchMissErrorsAllowNextDirectory) {
  const int errors[] = {ENODEV, ENOENT, ENOTDIR, EACCES, EIO,
                        ENOMEM, EMFILE, ENFILE,  EINTR};
  for (int opening = 0; opening != 2; ++opening) {
    for (int error : errors) {
      reset();
      state.readlink_error = ENOENT;
      Directory &dev = state.directories[0];
      (opening ? dev.open_error : dev.read_error) = error;
      bool fallback = error == ENODEV || error == ENOENT || error == ENOTDIR ||
                      error == EACCES;
      Buffer buffer;
      EXPECT_EQ(query(buffer), fallback ? 0 : error);
      if (fallback)
        buffer.contains(NAMES[1]);
      else
        buffer.unchanged();
      EXPECT_EQ(dev.opens, 1);
      EXPECT_EQ(dev.reads, opening ? 0 : 1);
      EXPECT_EQ(state.directories[1].opens, fallback ? 1 : 0);
      expect_retired(opening ? 0 : 1, fallback ? 1 : 0);
    }
  }
}

TEST(LlvmLibcMMIXTtynameSyscallTest, MalformedRecordsStopBeforeNextDirectory) {
  for (int defect = 0; defect != 9; ++defect) {
    reset();
    state.readlink_error = ENOENT;
    Directory &dev = state.directories[0];
    append_record(dev, "7");
    unsigned short length = 0;
    switch (defect) {
    case 0:
      dev.reported_count = 4097;
      break;
    case 1:
      dev.count = NAME_OFFSET;
      break;
    case 2:
    case 3:
    case 4:
      length = static_cast<unsigned short>(defect == 2   ? 0
                                           : defect == 3 ? NAME_OFFSET
                                                         : dev.count + 1);
      __builtin_memcpy(dev.entries + LENGTH_OFFSET, &length, sizeof(length));
      break;
    case 5:
      dev.entries[NAME_OFFSET] = '\0';
      break;
    case 6:
      for (size_t i = NAME_OFFSET; i < dev.count; ++i)
        dev.entries[i] = 'x';
      break;
    case 7:
      dev.entries[NAME_OFFSET] = '/';
      break;
    case 8:
      ++dev.relative.stx_ino;
      ++dev.count; // A valid nonmatching entry followed by a short header.
      break;
    }
    dev.close_error = EINTR;
    Buffer buffer;
    EXPECT_EQ(query(buffer), EIO);
    buffer.unchanged();
    EXPECT_EQ(dev.reads, 1);
    EXPECT_EQ(dev.relative_stats, defect == 8 ? 1 : 0);
    EXPECT_EQ(state.directories[1].opens, 0);
    expect_retired(1);
  }
}

TEST(LlvmLibcMMIXTtynameSyscallTest, CapacityFailureDoesNotPublishPartialName) {
  for (int fallback = 0; fallback != 2; ++fallback) {
    const char *expected = fallback ? NAMES[1] : PROC_NAME;
    size_t length = __builtin_strlen(expected);
    const size_t sizes[] = {0, 1, length, length + 1};
    for (size_t size : sizes) {
      reset();
      if (fallback)
        state.readlink_error = ENOENT;
      Buffer buffer;
      EXPECT_EQ(
          LIBC_NAMESPACE::tty_boundary_ttyname_r(state.fd, buffer.data(), size),
          size > length ? 0 : ERANGE);
      if (size > length)
        buffer.contains(expected);
      else
        buffer.unchanged();
      expect_retired(fallback ? 1 : 0, fallback ? 1 : 0);
    }
  }
}

TEST(LlvmLibcMMIXTtynameSyscallTest, NullBufferDoesNotStartDiscovery) {
  reset();
  EXPECT_EQ(LIBC_NAMESPACE::tty_boundary_ttyname_r(-1, nullptr, 0), EINVAL);
  EXPECT_EQ(state.ioctls, 0);
  EXPECT_EQ(state.cancel_calls, 0);
  expect_no_scan();
  expect_no_live_directory();
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
}

TEST(LlvmLibcMMIXTtynameSyscallTest,
     CloseFailureDoesNotPublishRetryOrFallback) {
  const int errors[] = {EIO, EINTR};
  for (int selected = 0; selected != 2; ++selected) {
    for (int error : errors) {
      reset();
      state.readlink_error = ENOENT;
      if (!selected)
        append_record(state.directories[0], "7");
      state.directories[selected].close_error = error;
      Buffer buffer;
      EXPECT_EQ(query(buffer), error);
      buffer.unchanged();
      EXPECT_EQ(state.directories[1].opens, selected);
      expect_retired(1, selected);
    }
  }
}

TEST(LlvmLibcMMIXTtynameSyscallTest,
     EarlierDiscoveryErrorSurvivesCloseFailure) {
  int Directory::*failures[] = {&Directory::read_error,
                                &Directory::relative_error,
                                &Directory::absolute_error};
  const int errors[] = {EIO, EINTR};
  for (int selected = 0; selected != 2; ++selected) {
    for (auto failure : failures) {
      for (int error : errors) {
        reset();
        state.readlink_error = ENOENT;
        if (!selected)
          append_record(state.directories[0], "7");
        Directory &directory = state.directories[selected];
        directory.*failure = error;
        directory.close_error = EBADF;
        Buffer buffer;
        EXPECT_EQ(query(buffer), error);
        buffer.unchanged();
        EXPECT_EQ(directory.reads,
                  failure != &Directory::read_error && error != EINTR ? 2 : 1);
        EXPECT_EQ(state.directories[1].opens, selected);
        expect_retired(1, selected);
      }
    }
  }
}

TEST(LlvmLibcMMIXTtynameSyscallTest,
     SearchMissSurvivesCloseErrorAndAllowsFallback) {
  reset();
  state.readlink_error = ENOENT;
  state.directories[0].close_error = EINTR;
  Buffer buffer;
  EXPECT_EQ(query(buffer), 0);
  buffer.contains(NAMES[1]);
  expect_retired(1, 1);
}

TEST(LlvmLibcMMIXTtynameSyscallTest,
     PreservesDisabledAndUninitializedCancellation) {
  reset();
  state.readlink_error = ENOENT;
  state.cancel_state = PTHREAD_CANCEL_DISABLE;
  Buffer buffer;
  EXPECT_EQ(query(buffer), 0);
  buffer.contains(NAMES[1]);
  expect_no_live_directory();
  EXPECT_EQ(state.directories[0].closes, 1);
  EXPECT_EQ(state.directories[1].closes, 1);
  EXPECT_EQ(state.cancel_calls, 2);
  EXPECT_EQ(state.cancel_state, int(PTHREAD_CANCEL_DISABLE));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);

  reset();
  state.readlink_error = ENOENT;
  state.no_cancel_context = true;
  EXPECT_EQ(query(buffer), 0);
  buffer.contains(NAMES[1]);
  expect_retired(1, 1);
}

TEST(LlvmLibcMMIXTtynameSyscallTest,
     CancellationRestoresAfterRetirementBeforeCopy) {
  for (int scenario = 0; scenario != 5; ++scenario) {
    reset();
    state.cancel_on_restore = true;
    if (scenario >= 1 && scenario <= 3)
      state.readlink_error = ENOENT;
    if (scenario == 2)
      state.directories[1].read_error = EIO;
    if (scenario == 3)
      state.directories[1].close_error = EINTR;
    if (scenario == 4)
      state.ioctl_error = EBADF;
    Buffer buffer;
    bool caught = false;
    try {
      (void)query(buffer);
    } catch (const Cancelled &) {
      caught = true;
    }
    EXPECT_TRUE(caught);
    buffer.unchanged();
    int closes = scenario >= 1 && scenario <= 3 ? 1 : 0;
    expect_retired(closes, closes);
  }
}
