//===-- MMIX PTY pathname syscall-boundary regression ---------------------===//
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

// Native transport injection only. Actual devpts mounts, locked O_PATH peers
// and namespace reachability still need the public guest fixture.
namespace {
using Metadata = LIBC_NAMESPACE::internal::kernel_statx_buf;
constexpr int MASTER = 41;
constexpr unsigned MASK = STATX_TYPE | STATX_INO;
constexpr const char *PROC_NAME = "/isolated/devpts/7";
constexpr const char *FALLBACK_NAME = "/dev/pts/7";

struct Cancelled {};

struct State {
  int peer = 0;
  unsigned index = 7;
  const char *link_name = PROC_NAME;
  int number_error = 0, peer_error = 0, peer_stat_error = 0;
  int readlink_error = 0, proc_stat_error = 0, fallback_stat_error = 0;
  int close_error = 0;
  int number_calls = 0, peer_calls = 0, peer_stat_calls = 0;
  int readlink_calls = 0, proc_stat_calls = 0, fallback_stat_calls = 0;
  int close_calls = 0, cancel_calls = 0;
  int cancel_state = PTHREAD_CANCEL_ENABLE;
  bool live = false, no_cancel_context = false, cancel_on_restore = false;
  Metadata peer_metadata{}, proc_metadata{}, fallback_metadata{};
} state;

void reset() {
  state = State{};
  state.peer_metadata.stx_mask = MASK;
  state.peer_metadata.stx_mode = S_IFCHR | 0620;
  state.peer_metadata.stx_ino = 19;
  state.peer_metadata.stx_dev_major = 0;
  state.peer_metadata.stx_dev_minor = 23;
  state.peer_metadata.stx_rdev_major = 136;
  state.peer_metadata.stx_rdev_minor = 7;
  state.proc_metadata = state.fallback_metadata = state.peer_metadata;
  LIBC_NAMESPACE::libc_errno = EDOM;
}

void expect_protected() {
  EXPECT_TRUE(state.no_cancel_context ||
              state.cancel_state == PTHREAD_CANCEL_DISABLE);
}

long dispatch(long number, long a1 = 0, long a2 = 0, long a3 = 0, long a4 = 0,
              long a5 = 0, long = 0) {
  expect_protected();
  if (number == SYS_ioctl) {
    if (a2 == TIOCGPTN) {
      ++state.number_calls;
      EXPECT_FALSE(state.live);
      if (a1 == -1)
        return -EBADF;
      EXPECT_EQ(a1, long(MASTER));
      if (state.number_error)
        return -state.number_error;
      *reinterpret_cast<unsigned *>(a3) = state.index;
      return 0;
    }
    EXPECT_EQ(a1, long(MASTER));
    EXPECT_EQ(a2, long(TIOCGPTPEER));
    EXPECT_EQ(a3, long(O_PATH | O_CLOEXEC));
    ++state.peer_calls;
    EXPECT_FALSE(state.live);
    if (state.peer_error)
      return -state.peer_error;
    state.live = true;
    return state.peer;
  }
  if (number == SYS_statx) {
    EXPECT_TRUE(state.live);
    EXPECT_EQ(a4, long(MASK));
    Metadata *result = reinterpret_cast<Metadata *>(a5);
    if (a3 == AT_EMPTY_PATH) {
      ++state.peer_stat_calls;
      EXPECT_EQ(a1, long(state.peer));
      EXPECT_STREQ(reinterpret_cast<const char *>(a2), "");
      if (state.peer_stat_error)
        return -state.peer_stat_error;
      *result = state.peer_metadata;
      return 0;
    }
    EXPECT_EQ(a1, long(AT_FDCWD));
    EXPECT_EQ(a3, 0L);
    const char *path = reinterpret_cast<const char *>(a2);
    if (__builtin_strcmp(path, state.link_name) == 0) {
      ++state.proc_stat_calls;
      if (state.proc_stat_error)
        return -state.proc_stat_error;
      *result = state.proc_metadata;
    } else {
      ++state.fallback_stat_calls;
      EXPECT_STREQ(path, state.index == ~0U ? "/dev/pts/4294967295"
                                          : FALLBACK_NAME);
      if (state.fallback_stat_error)
        return -state.fallback_stat_error;
      *result = state.fallback_metadata;
    }
    return 0;
  }
  if (number == SYS_readlinkat) {
    ++state.readlink_calls;
    EXPECT_TRUE(state.live);
    EXPECT_EQ(a1, long(AT_FDCWD));
    EXPECT_STREQ(reinterpret_cast<const char *>(a2),
                 state.peer == 0    ? "/proc/self/fd/0"
                 : state.peer == 63 ? "/proc/self/fd/63"
                                    : "/proc/self/fd/2147483647");
    EXPECT_EQ(a4, long(PATH_MAX));
    if (state.readlink_error)
      return -state.readlink_error;
    size_t length = __builtin_strlen(state.link_name);
    if (length > static_cast<size_t>(a4))
      length = static_cast<size_t>(a4);
    __builtin_memcpy(reinterpret_cast<void *>(a3), state.link_name, length);
    return static_cast<long>(length);
  }
  if (number == SYS_close) {
    EXPECT_TRUE(state.live);
    EXPECT_EQ(a1, long(state.peer));
    ++state.close_calls;
    state.live = false;
    return -state.close_error;
  }
  // No open, chmod, unlock, public close/cancellation point or directory scan.
  EXPECT_TRUE(false);
  return -ENOSYS;
}

void expect_retired(int closes = 1) {
  EXPECT_FALSE(state.live);
  EXPECT_EQ(state.close_calls, closes);
  EXPECT_EQ(state.cancel_calls, state.no_cancel_context ? 1 : 2);
  EXPECT_EQ(state.cancel_state, int(PTHREAD_CANCEL_ENABLE));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
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
    for (size_t i = 0; i <= length; ++i)
      EXPECT_EQ(bytes[i + 1], expected[i]);
    EXPECT_EQ(bytes[0], '#');
    for (size_t i = length + 2; i != sizeof(bytes); ++i)
      EXPECT_EQ(bytes[i], '#');
  }
};
} // namespace

namespace LIBC_NAMESPACE_DECL {
template <typename T, typename... Args>
T pts_test_syscall(long number, Args... args) {
  return static_cast<T>(dispatch(number, (long)args...));
}
namespace pts_test_syscalls {
template <typename T, typename... Args>
ErrorOr<T> pts_test_checked(long number, Args... args) {
  long result = dispatch(number, (long)args...);
  return result < 0 ? ErrorOr<T>(Error(int(-result)))
                    : ErrorOr<T>(static_cast<T>(result));
}
} // namespace pts_test_syscalls
namespace mmix {
int pts_test_set_cancel_state(int requested, int *saved) {
  ++state.cancel_calls;
  EXPECT_FALSE(state.live);
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

#ifndef LIBC_MMIX_PTY_CANCELLATION
#define LIBC_MMIX_PTY_CANCELLATION
#endif
#define linux_syscalls pts_test_syscalls
#define syscall_impl pts_test_syscall
#define syscall_checked pts_test_checked
#define set_cancel_state pts_test_set_cancel_state
#define ptsname_r pts_boundary_ptsname_r
#define ptsname pts_boundary_ptsname
#include "src/stdlib/linux/mmix/ptsname_r.cpp"
#include "src/stdlib/linux/mmix/ptsname.cpp"
#undef ptsname
#undef ptsname_r
#undef set_cancel_state
#undef syscall_checked
#undef syscall_impl
#undef linux_syscalls

TEST(LlvmLibcMMIXPtsnameSyscallTest, ProcPeerIdentityAndDescriptorExtremes) {
  const int peers[] = {0, 63, INT_MAX};
  for (int peer : peers) {
    reset();
    state.peer = peer;
    Buffer buffer;
    EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname_r(MASTER, buffer.data(),
                                                    buffer.capacity()),
              0);
    buffer.contains(PROC_NAME);
    EXPECT_EQ(state.number_calls, 1);
    EXPECT_EQ(state.peer_calls, 1);
    EXPECT_EQ(state.peer_stat_calls, 1);
    EXPECT_EQ(state.readlink_calls, 1);
    EXPECT_EQ(state.proc_stat_calls, 1);
    EXPECT_EQ(state.fallback_stat_calls, 0);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
    expect_retired();
  }
}

TEST(LlvmLibcMMIXPtsnameSyscallTest, MandatoryCapacityErrorsDoNotPublish) {
  const size_t length = __builtin_strlen(PROC_NAME);
  const size_t sizes[] = {0, 1, length, length + 1};
  for (size_t size : sizes) {
    reset();
    Buffer buffer;
    EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname_r(MASTER, buffer.data(), size),
              size > length ? 0 : ERANGE);
    if (size > length)
      buffer.contains(PROC_NAME);
    else
      buffer.unchanged();
    expect_retired();
  }
}

TEST(LlvmLibcMMIXPtsnameSyscallTest, MandatoryNullAndInvalidDescriptorErrors) {
  reset();
  EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname_r(MASTER, nullptr, 128), EINVAL);
  EXPECT_EQ(state.number_calls, 0);
  EXPECT_EQ(state.cancel_calls, 0);
  EXPECT_EQ(state.close_calls, 0);
  Buffer buffer;
  EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname_r(-1, buffer.data(),
                                                  buffer.capacity()),
            EBADF);
  buffer.unchanged();
  EXPECT_EQ(state.peer_calls, 0);
  expect_retired(0);
}

TEST(LlvmLibcMMIXPtsnameSyscallTest, PolicyNullPrecedesOtherInvalidInputs) {
  reset();
  EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname_r(-1, nullptr, 0), EINVAL);
  EXPECT_EQ(state.number_calls, 0);
  EXPECT_EQ(state.cancel_calls, 0);
  EXPECT_EQ(state.close_calls, 0);
}

TEST(LlvmLibcMMIXPtsnameSyscallTest, ManagerAndPeerFailuresDoNotCloseUnownedFd) {
  int State::*failures[] = {&State::number_error, &State::peer_error};
  const int errors[] = {EBADF, ENOTTY, EINVAL, EMFILE, ENFILE, EACCES, EINTR};
  for (auto failure : failures) {
    for (int error : errors) {
      reset();
      state.*failure = error;
      Buffer buffer;
      EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname_r(MASTER, buffer.data(),
                                                      buffer.capacity()),
                error);
      buffer.unchanged();
      EXPECT_EQ(state.peer_stat_calls, 0);
      EXPECT_EQ(state.peer_calls, failure == &State::number_error ? 0 : 1);
      expect_retired(0);
    }
  }
}

TEST(LlvmLibcMMIXPtsnameSyscallTest, NoProcfsFallbackRequiresVerifiedIdentity) {
  const int errors[] = {ENOENT, ENOTDIR, EACCES, ENOSYS};
  for (int error : errors) {
    reset();
    state.readlink_error = error;
    Buffer buffer;
    EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname_r(MASTER, buffer.data(),
                                                    buffer.capacity()),
              0);
    buffer.contains(FALLBACK_NAME);
    EXPECT_EQ(state.proc_stat_calls, 0);
    EXPECT_EQ(state.fallback_stat_calls, 1);
    expect_retired();
  }
  reset();
  state.readlink_error = ENOENT;
  state.index = ~0U;
  Buffer buffer;
  EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname_r(MASTER, buffer.data(),
                                                  buffer.capacity()),
            0);
  buffer.contains("/dev/pts/4294967295");
  expect_retired();
}

TEST(LlvmLibcMMIXPtsnameSyscallTest, RejectsEveryIdentityMismatchAndMissingField) {
  for (int defect = 0; defect != 8; ++defect) {
    reset();
    switch (defect) {
    case 0:
      state.proc_metadata.stx_mode = S_IFREG | 0600;
      break;
    case 1:
      ++state.proc_metadata.stx_ino;
      break;
    case 2:
      ++state.proc_metadata.stx_dev_major;
      break;
    case 3:
      ++state.proc_metadata.stx_dev_minor;
      break;
    case 4:
      ++state.proc_metadata.stx_rdev_major;
      break;
    case 5:
      ++state.proc_metadata.stx_rdev_minor;
      break;
    case 6:
      state.proc_metadata.stx_mask &= ~STATX_TYPE;
      break;
    case 7:
      state.proc_metadata.stx_mask &= ~STATX_INO;
      break;
    }
    state.fallback_metadata = state.proc_metadata;
    Buffer buffer;
    EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname_r(MASTER, buffer.data(),
                                                    buffer.capacity()),
              defect < 6 ? ENODEV : EIO);
    buffer.unchanged();
    EXPECT_EQ(state.proc_stat_calls, 1);
    EXPECT_EQ(state.fallback_stat_calls, 1);
    expect_retired();
  }
}

TEST(LlvmLibcMMIXPtsnameSyscallTest, InvalidPeerMetadataStopsDiscovery) {
  for (int defect = 0; defect != 3; ++defect) {
    reset();
    if (defect == 0)
      state.peer_metadata.stx_mode = S_IFREG | 0600;
    else
      state.peer_metadata.stx_mask &= ~(defect == 1 ? STATX_TYPE : STATX_INO);
    Buffer buffer;
    EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname_r(MASTER, buffer.data(),
                                                    buffer.capacity()),
              EIO);
    buffer.unchanged();
    EXPECT_EQ(state.readlink_calls, 0);
    EXPECT_EQ(state.fallback_stat_calls, 0);
    expect_retired();
  }
}

TEST(LlvmLibcMMIXPtsnameSyscallTest, UnreachableProcPathCanUseOnlyMatchingFallback) {
  const char *paths[] = {"", "relative/7", "/hidden/devpts/7 (deleted)"};
  for (const char *path : paths) {
    reset();
    state.link_name = path;
    state.proc_stat_error = ENOENT;
    Buffer buffer;
    EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname_r(MASTER, buffer.data(),
                                                    buffer.capacity()),
              0);
    buffer.contains(FALLBACK_NAME);
    EXPECT_EQ(state.fallback_stat_calls, 1);
    expect_retired();
  }
  reset();
  ++state.proc_metadata.stx_dev_minor;
  Buffer buffer;
  EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname_r(MASTER, buffer.data(),
                                                  buffer.capacity()),
            0);
  buffer.contains(FALLBACK_NAME);
  expect_retired();
}

TEST(LlvmLibcMMIXPtsnameSyscallTest, TruncatedProcPathIsNotPublishedOrStatQueried) {
  char path[PATH_MAX + 1];
  for (char &byte : path)
    byte = 'x';
  path[0] = '/';
  path[PATH_MAX] = '\0';
  const int errors[] = {0, ENOENT};
  for (int error : errors) {
    reset();
    state.link_name = path;
    state.fallback_stat_error = error;
    Buffer buffer;
    EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname_r(MASTER, buffer.data(),
                                                    buffer.capacity()),
              error);
    if (error)
      buffer.unchanged();
    else
      buffer.contains(FALLBACK_NAME);
    EXPECT_EQ(state.proc_stat_calls, 0);
    EXPECT_EQ(state.fallback_stat_calls, 1);
    expect_retired();
  }
}

TEST(LlvmLibcMMIXPtsnameSyscallTest, MaximumBoundedPathAndTerminatorFit) {
  char path[PATH_MAX];
  for (char &byte : path)
    byte = 'x';
  path[0] = '/';
  path[PATH_MAX - 1] = '\0';
  const size_t sizes[] = {PATH_MAX - 1, PATH_MAX};
  for (size_t size : sizes) {
    reset();
    state.link_name = path;
    char buffer[PATH_MAX + 2];
    for (char &byte : buffer)
      byte = '#';
    EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname_r(MASTER, buffer + 1, size),
              size == PATH_MAX ? 0 : ERANGE);
    EXPECT_EQ(buffer[0], '#');
    for (size_t i = 1; i != sizeof(buffer) - 1; ++i)
      EXPECT_EQ(buffer[i], size == PATH_MAX ? path[i - 1] : '#');
    EXPECT_EQ(buffer[PATH_MAX + 1], '#');
    EXPECT_EQ(state.proc_stat_calls, 1);
    EXPECT_EQ(state.fallback_stat_calls, 0);
    expect_retired();
  }
}

TEST(LlvmLibcMMIXPtsnameSyscallTest, InterruptedDiscoveryDoesNotRetryOrFallback) {
  int State::*failures[] = {&State::peer_stat_error, &State::readlink_error,
                           &State::proc_stat_error};
  for (auto failure : failures) {
    reset();
    state.*failure = EINTR;
    state.close_error = EIO;
    Buffer buffer;
    EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname_r(MASTER, buffer.data(),
                                                    buffer.capacity()),
              EINTR);
    buffer.unchanged();
    EXPECT_EQ(state.fallback_stat_calls, 0);
    expect_retired();
  }
}

TEST(LlvmLibcMMIXPtsnameSyscallTest, InaccessibleFallbackKeepsErrorAndClosesOnce) {
  const int errors[] = {ENOENT, ENOTDIR, EACCES, EIO, EINTR};
  for (int error : errors) {
    reset();
    state.readlink_error = ENOENT;
    state.fallback_stat_error = error;
    state.close_error = EBADF;
    Buffer buffer;
    EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname_r(MASTER, buffer.data(),
                                                    buffer.capacity()),
              error);
    buffer.unchanged();
    EXPECT_EQ(state.fallback_stat_calls, 1);
    expect_retired();
  }
}

TEST(LlvmLibcMMIXPtsnameSyscallTest, CloseFailureDoesNotPublishOrRetry) {
  const int errors[] = {EINTR, EIO};
  for (int error : errors) {
    reset();
    state.close_error = error;
    Buffer buffer;
    EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname_r(MASTER, buffer.data(),
                                                    buffer.capacity()),
              error);
    buffer.unchanged();
    expect_retired();
  }
}

TEST(LlvmLibcMMIXPtsnameSyscallTest, WrapperErrnoAndIndependentCallerStorage) {
  reset();
  Buffer buffer;
  EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname_r(MASTER, buffer.data(),
                                                  buffer.capacity()),
            0);
  buffer.contains(PROC_NAME);
  const char *borrowed = LIBC_NAMESPACE::pts_boundary_ptsname(MASTER);
  ASSERT_NE(borrowed, static_cast<const char *>(nullptr));
  EXPECT_STREQ(borrowed, PROC_NAME);
  state.link_name = "/another/devpts/7";
  borrowed = LIBC_NAMESPACE::pts_boundary_ptsname(MASTER);
  ASSERT_NE(borrowed, static_cast<const char *>(nullptr));
  EXPECT_STREQ(borrowed, state.link_name);
  buffer.contains(PROC_NAME);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
  EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname(-1),
            static_cast<char *>(nullptr));
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EBADF);
  buffer.contains(PROC_NAME);
  EXPECT_FALSE(state.live);
  EXPECT_EQ(state.close_calls, 3);
}

TEST(LlvmLibcMMIXPtsnameSyscallTest, PreservesDisabledAndUninitializedCancellation) {
  reset();
  state.cancel_state = PTHREAD_CANCEL_DISABLE;
  Buffer buffer;
  EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname_r(MASTER, buffer.data(),
                                                  buffer.capacity()),
            0);
  buffer.contains(PROC_NAME);
  EXPECT_EQ(state.cancel_state, int(PTHREAD_CANCEL_DISABLE));
  EXPECT_EQ(state.cancel_calls, 2);
  EXPECT_EQ(state.close_calls, 1);
  EXPECT_FALSE(state.live);
  reset();
  state.no_cancel_context = true;
  EXPECT_EQ(LIBC_NAMESPACE::pts_boundary_ptsname_r(MASTER, buffer.data(),
                                                  buffer.capacity()),
            0);
  buffer.contains(PROC_NAME);
  expect_retired();
}

TEST(LlvmLibcMMIXPtsnameSyscallTest, CancellationRestorationFollowsCloseAndPrecedesCopy) {
  const int errors[] = {0, EIO};
  for (int error : errors) {
    reset();
    state.peer_stat_error = error;
    state.cancel_on_restore = true;
    Buffer buffer;
    bool caught = false;
    try {
      (void)LIBC_NAMESPACE::pts_boundary_ptsname_r(MASTER, buffer.data(),
                                                 buffer.capacity());
    } catch (const Cancelled &) {
      caught = true;
    }
    EXPECT_TRUE(caught);
    buffer.unchanged();
    expect_retired();
  }
}

TEST(LlvmLibcMMIXPtsnameSyscallTest, BothEntrypointsUnwindCallerAfterRetirement) {
  for (int wrapper = 0; wrapper != 2; ++wrapper) {
    reset();
    state.cancel_on_restore = true;
    int destroyed = 0;
    struct Guard {
      int &destroyed;
      ~Guard() {
        EXPECT_FALSE(state.live);
        EXPECT_EQ(state.close_calls, 1);
        ++destroyed;
      }
    };
    Buffer buffer;
    bool caught = false;
    try {
      Guard guard{destroyed};
      if (wrapper)
        (void)LIBC_NAMESPACE::pts_boundary_ptsname(MASTER);
      else
        (void)LIBC_NAMESPACE::pts_boundary_ptsname_r(MASTER, buffer.data(),
                                                   buffer.capacity());
    } catch (const Cancelled &) {
      caught = true;
    }
    EXPECT_TRUE(caught);
    EXPECT_EQ(destroyed, 1);
    buffer.unchanged();
    expect_retired();
  }
}
