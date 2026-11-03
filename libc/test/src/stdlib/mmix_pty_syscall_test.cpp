//===-- MMIX PTY syscall-boundary regression source -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/sys_ioctl_macros.h"
#include "hdr/sys_stat_macros.h"
#include "hdr/types/gid_t.h"
#include "hdr/types/uid_t.h"
#include "src/__support/OSUtil/linux/stat/kernel_statx_types.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/cancellation.h"
#include "src/__support/threads/linux/mmix/cancellation_point.h"
#include "test/UnitTest/Test.h"
#include <linux/stat.h>
#include <pthread.h>
#include <sys/syscall.h>

// All kernel and cancellation transports are injected. This source neither
// opens a real terminal nor establishes O_PATH/PTY kernel execution evidence.
namespace {
constexpr int MASTER = 41;
struct Cancelled {};
struct State {
  int peer = 0;
  int open_result = 0, open_error = 0, open_calls = 0, open_flags = 0;
  int number_error = 0, peer_error = 0, mode_error = 0, owner_error = 0;
  int before_error = 0, after_error = 0, close_error = 0, unlock_error = 0;
  int number_calls = 0, peer_calls = 0, stat_calls = 0, mode_calls = 0;
  int owner_calls = 0, close_calls = 0, unlock_calls = 0, cancel_calls = 0;
  int cancel_state = PTHREAD_CANCEL_ENABLE;
  bool no_cancel_context = false, live = false, locked = true;
  bool initial_lock = true;
  bool ignore_mode = false, ignore_owner = false;
  bool cancel_on_restore = false;
  unsigned mask = STATX_TYPE | STATX_MODE | STATX_UID;
  unsigned mode = S_IFCHR | 0600;
  uid_t real_uid = 1001, owner = 2002;
} state;

void reset() {
  state = State{};
  LIBC_NAMESPACE::libc_errno = EDOM;
}

void expect_protected_peer(long fd) {
  EXPECT_EQ(fd, long(state.peer));
  EXPECT_TRUE(state.live);
  EXPECT_TRUE(state.no_cancel_context ||
              state.cancel_state == PTHREAD_CANCEL_DISABLE);
  EXPECT_EQ(state.locked, state.initial_lock);
}

long dispatch(long number, long a1 = 0, long a2 = 0, long a3 = 0, long a4 = 0,
              long a5 = 0, long = 0) {
  if (number == SYS_openat) {
    ++state.open_calls;
    EXPECT_EQ(a1, long(AT_FDCWD));
    EXPECT_STREQ(reinterpret_cast<const char *>(a2), "/dev/ptmx");
    EXPECT_EQ(a4, 0L);
    state.open_flags = int(a3);
    return state.open_error ? -state.open_error : state.open_result;
  }
  if (number == SYS_ioctl) {
    EXPECT_EQ(a1, long(MASTER));
    if (a2 == TIOCGPTN) {
      ++state.number_calls;
      EXPECT_TRUE(state.no_cancel_context ||
                  state.cancel_state == PTHREAD_CANCEL_DISABLE);
      if (state.number_error)
        return -state.number_error;
      *reinterpret_cast<unsigned int *>(a3) = 7;
      return 0;
    }
    if (a2 == TIOCGPTPEER) {
      ++state.peer_calls;
      EXPECT_EQ(a3, long(O_PATH | O_CLOEXEC));
      EXPECT_TRUE(state.no_cancel_context ||
                  state.cancel_state == PTHREAD_CANCEL_DISABLE);
      EXPECT_FALSE(state.live);
      EXPECT_EQ(state.locked, state.initial_lock);
      if (state.peer_error)
        return -state.peer_error;
      state.live = true;
      return state.peer;
    }
    if (a2 == TIOCSPTLCK) {
      ++state.unlock_calls;
      EXPECT_EQ(*reinterpret_cast<int *>(a3), 0);
      if (state.unlock_error)
        return -state.unlock_error;
      state.locked = false;
      return 0;
    }
  }
  if (number == SYS_statx) {
    expect_protected_peer(a1);
    EXPECT_STREQ(reinterpret_cast<const char *>(a2), "");
    EXPECT_EQ(a3, long(AT_EMPTY_PATH));
    EXPECT_EQ(a4, long(STATX_TYPE | STATX_MODE | STATX_UID));
    int error = ++state.stat_calls == 1 ? state.before_error : state.after_error;
    if (error)
      return -error;
    auto &metadata =
        *reinterpret_cast<LIBC_NAMESPACE::internal::kernel_statx_buf *>(a5);
    metadata = {};
    metadata.stx_mask = state.mask;
    metadata.stx_mode = static_cast<decltype(metadata.stx_mode)>(state.mode);
    metadata.stx_uid = state.owner;
    return 0;
  }
  if (number == SYS_getuid) {
    EXPECT_TRUE(state.live);
    return state.real_uid;
  }
  if (number == SYS_fchmodat2) {
    expect_protected_peer(a1);
    EXPECT_STREQ(reinterpret_cast<const char *>(a2), "");
    EXPECT_EQ(a3, 0620L);
    EXPECT_EQ(a4, long(AT_EMPTY_PATH));
    EXPECT_EQ(state.owner_calls, 0);
    ++state.mode_calls;
    if (state.mode_error)
      return -state.mode_error;
    if (!state.ignore_mode)
      state.mode = S_IFCHR | 0620;
    return 0;
  }
  if (number == SYS_fchownat) {
    expect_protected_peer(a1);
    EXPECT_STREQ(reinterpret_cast<const char *>(a2), "");
    EXPECT_EQ(static_cast<uid_t>(a3), state.real_uid);
    EXPECT_EQ(static_cast<gid_t>(a4), static_cast<gid_t>(-1));
    EXPECT_EQ(a5, long(AT_EMPTY_PATH));
    ++state.owner_calls;
    if (state.owner_error)
      return -state.owner_error;
    if (!state.ignore_owner)
      state.owner = static_cast<uid_t>(a3);
    return 0;
  }
  if (number == SYS_close) {
    expect_protected_peer(a1);
    ++state.close_calls;
    state.live = false;
    return -state.close_error;
  }
  // Includes pathname repair, unlock during grant, legacy chmod fallback,
  // credential mutation and effective-UID queries.
  EXPECT_TRUE(false);
  return -ENOSYS;
}

void expect_retired() {
  EXPECT_FALSE(state.live);
  EXPECT_TRUE(state.locked);
  EXPECT_EQ(state.close_calls, 1);
  EXPECT_EQ(state.unlock_calls, 0);
  EXPECT_EQ(state.cancel_calls, 2);
  EXPECT_EQ(state.cancel_state, int(PTHREAD_CANCEL_ENABLE));
}
} // namespace

namespace LIBC_NAMESPACE_DECL {
template <typename T, typename... Args>
T pty_test_syscall(long number, Args... args) {
  return static_cast<T>(dispatch(number, (long)args...));
}
namespace pty_test_syscalls {
template <typename T, typename... Args>
ErrorOr<T> pty_test_checked(long number, Args... args) {
  long result = dispatch(number, (long)args...);
  return result < 0 ? ErrorOr<T>(Error(int(-result)))
                    : ErrorOr<T>(static_cast<T>(result));
}
} // namespace pty_test_syscalls

namespace mmix {
int pty_test_set_cancel_state(int requested, int *saved) {
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
long pty_test_cancellation_syscall(unsigned long number, long a1, long a2,
                                  long a3, long a4) {
  EXPECT_EQ(number, static_cast<unsigned long>(SYS_openat));
  return dispatch(number, a1, a2, a3, a4);
}
} // namespace mmix
int pty_boundary_open(const char *, int, ...);
} // namespace LIBC_NAMESPACE_DECL

// Keep the production wrappers and entrypoints; replace only their transports
// and symbol names, independently of any already linked public providers.
#ifndef LIBC_MMIX_PTY_CANCELLATION
#define LIBC_MMIX_PTY_CANCELLATION
#endif
#define linux_syscalls pty_test_syscalls
#define syscall_impl pty_test_syscall
#define syscall_checked pty_test_checked
#define set_cancel_state pty_test_set_cancel_state
#define cancellation_syscall pty_test_cancellation_syscall
#define open pty_boundary_open
#define posix_openpt pty_boundary_posix_openpt
#define grantpt pty_boundary_grantpt
#define unlockpt pty_boundary_unlockpt
#include "src/fcntl/linux/mmix/cancellation_open.cpp"
#include "src/stdlib/linux/mmix/posix_openpt.cpp"
#include "src/stdlib/linux/mmix/grantpt.cpp"
#include "src/stdlib/linux/mmix/unlockpt.cpp"
#undef unlockpt
#undef grantpt
#undef posix_openpt
#undef open
#undef cancellation_syscall
#undef set_cancel_state
#undef syscall_checked
#undef syscall_impl
#undef linux_syscalls

TEST(LlvmLibcMMIXPtySyscallTest, OpenFlagsAndLowestDescriptorPassThrough) {
  const int extras[] = {0, O_NOCTTY, O_CLOEXEC, O_NOCTTY | O_CLOEXEC};
  for (int extra : extras) {
    reset();
    EXPECT_EQ(LIBC_NAMESPACE::pty_boundary_posix_openpt(O_RDWR | extra), 0);
    EXPECT_EQ(state.open_flags, O_RDWR | extra);
    EXPECT_EQ(state.open_calls, 1);
    EXPECT_EQ(state.cancel_calls, 0);
    EXPECT_EQ(state.number_calls, 0);
    EXPECT_EQ(state.close_calls, 0);
  }
}

TEST(LlvmLibcMMIXPtySyscallTest, OpenRejectsUnspecifiedFlagsWithoutAllocation) {
  const int rejected[] = {O_RDONLY, O_WRONLY, O_RDWR | O_NONBLOCK,
                          O_RDWR | O_CREAT, O_RDWR | O_PATH};
  for (int flags : rejected) {
    reset();
    EXPECT_EQ(LIBC_NAMESPACE::pty_boundary_posix_openpt(flags), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EINVAL);
    EXPECT_EQ(state.open_calls, 0);
  }
}

TEST(LlvmLibcMMIXPtySyscallTest, OpenErrorsAreNotReclassifiedOrRetried) {
  const int errors[] = {EMFILE, ENFILE, EAGAIN, ENOSPC, EACCES, EINTR};
  for (int error : errors) {
    reset();
    state.open_error = error;
    EXPECT_EQ(LIBC_NAMESPACE::pty_boundary_posix_openpt(O_RDWR), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
    EXPECT_EQ(state.open_calls, 1);
    EXPECT_EQ(state.close_calls, 0);
  }
}

TEST(LlvmLibcMMIXPtySyscallTest, GrantRepairsActualSlaveBeforeUnlocking) {
  const int peers[] = {0, 63};
  for (int peer : peers) {
    reset();
    state.peer = peer;
    EXPECT_EQ(LIBC_NAMESPACE::pty_boundary_grantpt(MASTER), 0);
    EXPECT_EQ(state.mode, unsigned(S_IFCHR | 0620));
    EXPECT_EQ(state.owner, state.real_uid);
    EXPECT_EQ(state.number_calls, 1);
    EXPECT_EQ(state.peer_calls, 1);
    EXPECT_EQ(state.stat_calls, 2);
    EXPECT_EQ(state.mode_calls, 1);
    EXPECT_EQ(state.owner_calls, 1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EDOM);
    expect_retired();
  }
}

TEST(LlvmLibcMMIXPtySyscallTest, GrantUnwindsOnlyAfterRetiringPeer) {
  const int errors[] = {0, EIO};
  for (int error : errors) {
    reset();
    state.before_error = error;
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
    bool caught = false;
    try {
      Guard guard{destroyed};
      (void)LIBC_NAMESPACE::pty_boundary_grantpt(MASTER);
    } catch (const Cancelled &) {
      caught = true;
    }
    EXPECT_TRUE(caught);
    EXPECT_EQ(destroyed, 1);
    expect_retired();
  }
}

TEST(LlvmLibcMMIXPtySyscallTest, GrantSkipsOnlyCorrectMetadata) {
  const bool choices[] = {false, true};
  for (bool correct_mode : choices) {
    for (bool correct_owner : choices) {
      reset();
      state.real_uid = static_cast<uid_t>(0x80000001U);
      if (correct_owner)
        state.owner = state.real_uid;
      if (correct_mode)
        state.mode = S_IFCHR | 0620;
      EXPECT_EQ(LIBC_NAMESPACE::pty_boundary_grantpt(MASTER), 0);
      EXPECT_EQ(state.mode_calls, correct_mode ? 0 : 1);
      EXPECT_EQ(state.owner_calls, correct_owner ? 0 : 1);
      EXPECT_EQ(state.stat_calls, 2);
      expect_retired();
    }
  }
}

TEST(LlvmLibcMMIXPtySyscallTest, GrantRemovesExcessPermissions) {
  const unsigned modes[] = {0000, 0666, 04777, 06720};
  for (unsigned mode : modes) {
    reset();
    state.mode = S_IFCHR | mode;
    state.owner = state.real_uid;
    EXPECT_EQ(LIBC_NAMESPACE::pty_boundary_grantpt(MASTER), 0);
    EXPECT_EQ(state.mode, unsigned(S_IFCHR | 0620));
    EXPECT_EQ(state.mode_calls, 1);
    EXPECT_EQ(state.owner_calls, 0);
    expect_retired();
  }
}

TEST(LlvmLibcMMIXPtySyscallTest, GrantDoesNotRelockAnUnlockedSlave) {
  reset();
  state.locked = state.initial_lock = false;
  EXPECT_EQ(LIBC_NAMESPACE::pty_boundary_grantpt(MASTER), 0);
  EXPECT_FALSE(state.locked);
  EXPECT_FALSE(state.live);
  EXPECT_EQ(state.unlock_calls, 0);
  EXPECT_EQ(state.close_calls, 1);
  EXPECT_EQ(state.cancel_state, int(PTHREAD_CANCEL_ENABLE));
}

TEST(LlvmLibcMMIXPtySyscallTest, GrantDoesNotCloseAnUnacquiredPeer) {
  const int manager_errors[] = {EBADF, ENOTTY, EINVAL, EINTR};
  for (int error : manager_errors) {
    reset();
    state.number_error = error;
    EXPECT_EQ(LIBC_NAMESPACE::pty_boundary_grantpt(MASTER), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error == ENOTTY ? EINVAL : error);
    EXPECT_EQ(state.peer_calls, 0);
    EXPECT_EQ(state.close_calls, 0);
    EXPECT_EQ(state.cancel_state, int(PTHREAD_CANCEL_ENABLE));
  }
  const int peer_errors[] = {ENODEV, ENOTTY, EINVAL, EMFILE,
                            ENFILE, EACCES, EINTR};
  for (int error : peer_errors) {
    reset();
    state.peer_error = error;
    EXPECT_EQ(LIBC_NAMESPACE::pty_boundary_grantpt(MASTER), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
    EXPECT_EQ(state.peer_calls, 1);
    EXPECT_EQ(state.stat_calls, 0);
    EXPECT_EQ(state.close_calls, 0);
    EXPECT_EQ(state.cancel_state, int(PTHREAD_CANCEL_ENABLE));
  }
}

TEST(LlvmLibcMMIXPtySyscallTest, GrantFailureClosesOnceAndPreservesFirstError) {
  int State::*failures[] = {&State::before_error, &State::mode_error,
                           &State::owner_error, &State::after_error};
  const int errors[] = {EPERM, EACCES, EROFS, EINTR, EIO};
  for (auto failure : failures) {
    for (int error : errors) {
      reset();
      state.*failure = error;
      state.close_error = EBADF;
      EXPECT_EQ(LIBC_NAMESPACE::pty_boundary_grantpt(MASTER), -1);
      EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
      if (failure == &State::before_error || failure == &State::mode_error)
        EXPECT_EQ(state.owner_calls, 0);
      expect_retired();
    }
  }
}

TEST(LlvmLibcMMIXPtySyscallTest, GrantDoesNotEmulateMissingEmptyPathChmod) {
  reset();
  state.mode_error = ENOSYS;
  EXPECT_EQ(LIBC_NAMESPACE::pty_boundary_grantpt(MASTER), -1);
  EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EOPNOTSUPP);
  EXPECT_EQ(state.mode_calls, 1);
  EXPECT_EQ(state.owner_calls, 0);
  expect_retired();
}

TEST(LlvmLibcMMIXPtySyscallTest, GrantRejectsIncompleteOrWrongMetadata) {
  for (int defect = 0; defect != 5; ++defect) {
    reset();
    if (defect == 0)
      state.mask &= ~STATX_UID;
    if (defect == 1)
      state.mask &= ~STATX_MODE;
    if (defect == 2)
      state.mode = S_IFREG | 0600;
    state.ignore_mode = defect == 3;
    state.ignore_owner = defect == 4;
    EXPECT_EQ(LIBC_NAMESPACE::pty_boundary_grantpt(MASTER), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EIO);
    expect_retired();
  }
}

TEST(LlvmLibcMMIXPtySyscallTest, GrantReportsCloseFailureWithoutRetry) {
  const int errors[] = {EINTR, EIO};
  for (int error : errors) {
    reset();
    state.close_error = error;
    EXPECT_EQ(LIBC_NAMESPACE::pty_boundary_grantpt(MASTER), -1);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
    EXPECT_EQ(state.owner, state.real_uid);
    EXPECT_EQ(state.mode, unsigned(S_IFCHR | 0620));
    expect_retired();
  }
}

TEST(LlvmLibcMMIXPtySyscallTest, GrantPreservesDisabledOrUninitializedState) {
  reset();
  state.cancel_state = PTHREAD_CANCEL_DISABLE;
  EXPECT_EQ(LIBC_NAMESPACE::pty_boundary_grantpt(MASTER), 0);
  EXPECT_EQ(state.cancel_state, int(PTHREAD_CANCEL_DISABLE));
  EXPECT_EQ(state.cancel_calls, 2);
  EXPECT_EQ(state.close_calls, 1);
  EXPECT_FALSE(state.live);
  reset();
  state.no_cancel_context = true;
  EXPECT_EQ(LIBC_NAMESPACE::pty_boundary_grantpt(MASTER), 0);
  EXPECT_EQ(state.cancel_calls, 1);
  EXPECT_EQ(state.close_calls, 1);
  EXPECT_FALSE(state.live);
}

TEST(LlvmLibcMMIXPtySyscallTest, UnlockUsesPointerAndNoCancellationPoint) {
  const int errors[] = {0, EBADF, ENOTTY, EINVAL, EINTR};
  for (int error : errors) {
    reset();
    state.unlock_error = error;
    EXPECT_EQ(LIBC_NAMESPACE::pty_boundary_unlockpt(MASTER), error ? -1 : 0);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno),
              error == ENOTTY ? EINVAL : error ? error : EDOM);
    EXPECT_EQ(state.unlock_calls, 1);
    EXPECT_EQ(state.cancel_calls, 0);
    EXPECT_EQ(state.peer_calls, 0);
    EXPECT_EQ(state.close_calls, 0);
    EXPECT_EQ(state.locked, error != 0);
  }
}
