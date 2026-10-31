//===-- Tests for private Linux AIO submission capture --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/CPP/new.h"
#include "src/__support/OSUtil/linux/aio_submission_resources.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;
using LIBC_NAMESPACE::aio_submission_resources::LinuxOperations;

namespace {

constexpr int SOURCE_FD = 41;
constexpr unsigned PERSONALITY = 65535;

enum Event { Register, Duplicate, Close, Unregister };

struct State {
  Event events[32] = {};
  unsigned arguments[32] = {};
  unsigned count = 0;
  int duplicate_fd = 73;
  int register_error = 0, duplicate_error = 0;
  int close_error = 0, unregister_error = 0;
  bool personality_owned = false, fd_owned = false;
  bool fd_number_reused = false;
  const void *owner = nullptr;
  bool (*owner_ready)(const void *) = nullptr;

  void record(Event event, unsigned argument = 0) {
    // Neither acquisition nor cleanup may expose a usable partial capture.
    if (owner_ready)
      EXPECT_FALSE(owner_ready(owner));
    EXPECT_LT(count, 32U);
    if (count < 32) {
      events[count] = event;
      arguments[count++] = argument;
    }
  }
};

struct FakeRing {
  State &state;

  ErrorOr<unsigned> register_personality() {
    state.record(Register);
    EXPECT_FALSE(state.personality_owned);
    if (state.register_error)
      return Error(state.register_error);
    state.personality_owned = true;
    return PERSONALITY;
  }

  ErrorOr<void> unregister_personality(unsigned id) {
    state.record(Unregister, id);
    EXPECT_EQ(id, PERSONALITY);
    EXPECT_TRUE(state.personality_owned);
    if (state.unregister_error)
      return Error(state.unregister_error);
    state.personality_owned = false;
    return {};
  }
};

struct FakeOperations {
  State &state;

  ErrorOr<int> duplicate(int fd) {
    state.record(Duplicate, static_cast<unsigned>(fd));
    EXPECT_EQ(fd, SOURCE_FD);
    EXPECT_TRUE(state.personality_owned);
    EXPECT_FALSE(state.fd_owned);
    if (state.duplicate_error)
      return Error(state.duplicate_error);
    state.fd_owned = true;
    state.fd_number_reused = false;
    return state.duplicate_fd;
  }

  ErrorOr<int> close(int fd) {
    state.record(Close, static_cast<unsigned>(fd));
    EXPECT_EQ(fd, state.duplicate_fd);
    EXPECT_NE(fd, SOURCE_FD);
    EXPECT_TRUE(state.fd_owned);
    EXPECT_FALSE(state.fd_number_reused);
    // Model immediate reuse, even when Linux close reports EINTR or EIO.
    state.fd_owned = false;
    state.fd_number_reused = true;
    if (state.close_error)
      return Error(state.close_error);
    return 0;
  }
};

using Owner = LIBC_NAMESPACE::AioSubmissionResources<FakeRing, FakeOperations>;

static_assert(!__is_constructible(Owner, const Owner &));
static_assert(!__is_constructible(Owner, Owner &&));
static_assert(!__is_assignable(Owner &, const Owner &));
static_assert(!__is_assignable(Owner &, Owner &&));

void observe(State &state, const Owner &owner) {
  state.owner = &owner;
  state.owner_ready = [](const void *pointer) {
    const auto &owner = *static_cast<const Owner *>(pointer);
    EXPECT_EQ(owner.descriptor(), -1);
    EXPECT_EQ(owner.personality(), 0U);
    return owner.ready();
  };
}

template <class T> void expect_error(const ErrorOr<T> &result, int error) {
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error);
}

void expect_unready(const Owner &owner, bool drained) {
  EXPECT_FALSE(owner.ready());
  EXPECT_EQ(owner.drained(), drained);
  EXPECT_EQ(owner.descriptor(), -1);
  EXPECT_EQ(owner.personality(), 0U);
}

void expect_event(const State &state, unsigned index, Event event,
                  unsigned argument = 0) {
  ASSERT_LT(index, state.count);
  EXPECT_EQ(static_cast<int>(state.events[index]), static_cast<int>(event));
  EXPECT_EQ(state.arguments[index], argument);
}

struct ErrnoGuard {
  int saved = libc_errno;
  ErrnoGuard() { libc_errno = EDOM; }
  ~ErrnoGuard() { libc_errno = saved; }
  void check() { EXPECT_EQ(int(libc_errno), EDOM); }
};

} // namespace

TEST(LlvmLibcAioSubmissionResourcesTest,
     ChildQuarantineDoesNotUnregisterParent) {
  const int errors[] = {0, EINTR, EIO};
  for (int error : errors) {
    State state;
    state.close_error = error;
    FakeRing ring{state};
    // Model copied process storage: quarantine intentionally forbids its
    // destructor, because the personality still belongs to the parent ring.
    alignas(Owner) unsigned char storage[sizeof(Owner)];
    auto *owner = new (storage) Owner(ring, FakeOperations{state});
    observe(state, *owner);
    ASSERT_TRUE(bool(owner->capture(SOURCE_FD)));
    auto result = owner->quarantine_child();
    EXPECT_EQ(result ? 0 : result.error(), error);
    EXPECT_FALSE(owner->ready());
    EXPECT_FALSE(owner->drained());
    EXPECT_FALSE(state.fd_owned);
    EXPECT_TRUE(state.personality_owned);
    EXPECT_EQ(state.count, 3U);
    expect_event(state, 2, Close, unsigned(state.duplicate_fd));
    ASSERT_TRUE(bool(owner->quarantine_child()));
    EXPECT_EQ(state.count, 3U);
  }
}

TEST(LlvmLibcAioSubmissionResourcesTest, EmptyReleaseAndDestructionDoNothing) {
  State state;
  FakeRing ring{state};
  {
    Owner owner(ring, FakeOperations{state});
    observe(state, owner);
    expect_unready(owner, true);
    EXPECT_EQ(owner.error(), 0);
    EXPECT_EQ(owner.cleanup_error(), 0);
    EXPECT_TRUE(owner.release().has_value());
  }
  EXPECT_EQ(state.count, 0U);
}

TEST(LlvmLibcAioSubmissionResourcesTest, CaptureOrderFdZeroAndReuse) {
  ErrnoGuard errno_guard;
  State state;
  state.duplicate_fd = 0;
  FakeRing ring{state};
  Owner owner(ring, FakeOperations{state});
  observe(state, owner);
  ASSERT_TRUE(owner.capture(SOURCE_FD).has_value());
  EXPECT_TRUE(owner.ready());
  EXPECT_FALSE(owner.drained());
  EXPECT_EQ(owner.descriptor(), 0);
  EXPECT_EQ(owner.personality(), PERSONALITY);
  EXPECT_EQ(state.count, 2U);
  expect_event(state, 0, Register);
  expect_event(state, 1, Duplicate, SOURCE_FD);
  expect_error(owner.capture(SOURCE_FD), EBUSY);
  EXPECT_TRUE(owner.ready());
  EXPECT_EQ(owner.descriptor(), 0);
  EXPECT_EQ(owner.personality(), PERSONALITY);
  EXPECT_EQ(owner.error(), 0);
  EXPECT_EQ(state.count, 2U);
  EXPECT_TRUE(owner.release().has_value());
  expect_unready(owner, true);
  EXPECT_EQ(state.count, 4U);
  expect_event(state, 2, Close, 0);
  expect_event(state, 3, Unregister, PERSONALITY);
  EXPECT_TRUE(owner.release().has_value());
  EXPECT_EQ(state.count, 4U);
  ASSERT_TRUE(owner.capture(SOURCE_FD).has_value());
  EXPECT_TRUE(owner.release().has_value());
  EXPECT_EQ(state.count, 8U);
  expect_event(state, 4, Register);
  expect_event(state, 5, Duplicate, SOURCE_FD);
  expect_event(state, 6, Close, 0);
  expect_event(state, 7, Unregister, PERSONALITY);
  errno_guard.check();
}

TEST(LlvmLibcAioSubmissionResourcesTest, RegisterFailureNeverDuplicates) {
  const int errors[] = {EPERM, ENOMEM, ENOSPC, EBADF, EINTR};
  for (int error : errors) {
    ErrnoGuard errno_guard;
    State state;
    state.register_error = error;
    FakeRing ring{state};
    Owner owner(ring, FakeOperations{state});
    observe(state, owner);
    expect_error(owner.capture(SOURCE_FD), error);
    expect_unready(owner, true);
    EXPECT_EQ(owner.error(), error);
    EXPECT_EQ(owner.cleanup_error(), 0);
    EXPECT_TRUE(owner.release().has_value());
    EXPECT_EQ(owner.error(), error);
    EXPECT_EQ(state.count, 1U);
    expect_event(state, 0, Register);
    state.register_error = 0;
    EXPECT_TRUE(owner.capture(SOURCE_FD).has_value());
    EXPECT_EQ(owner.error(), 0);
    EXPECT_TRUE(owner.release().has_value());
    errno_guard.check();
  }
}

TEST(LlvmLibcAioSubmissionResourcesTest, DuplicateFailureRollsBackPersonality) {
  const int errors[] = {EBADF, EMFILE, ENFILE, ENOMEM, EINTR};
  for (int error : errors) {
    ErrnoGuard errno_guard;
    State state;
    state.duplicate_error = error;
    FakeRing ring{state};
    Owner owner(ring, FakeOperations{state});
    observe(state, owner);
    expect_error(owner.capture(SOURCE_FD), error);
    expect_unready(owner, true);
    EXPECT_EQ(owner.error(), error);
    EXPECT_EQ(owner.cleanup_error(), 0);
    EXPECT_EQ(state.count, 3U);
    expect_event(state, 0, Register);
    expect_event(state, 1, Duplicate, SOURCE_FD);
    expect_event(state, 2, Unregister, PERSONALITY);
    EXPECT_TRUE(owner.release().has_value());
    EXPECT_EQ(state.count, 3U);
    EXPECT_EQ(owner.error(), error);
    errno_guard.check();
  }
}

TEST(LlvmLibcAioSubmissionResourcesTest,
     RollbackFailureRetainsOnlyPersonality) {
  ErrnoGuard errno_guard;
  State state;
  state.duplicate_error = EMFILE;
  state.unregister_error = EIO;
  FakeRing ring{state};
  Owner owner(ring, FakeOperations{state});
  observe(state, owner);
  expect_error(owner.capture(SOURCE_FD), EMFILE);
  expect_unready(owner, false);
  EXPECT_EQ(owner.error(), EMFILE);
  EXPECT_EQ(owner.cleanup_error(), EIO);
  EXPECT_TRUE(state.personality_owned);
  EXPECT_FALSE(state.fd_owned);
  EXPECT_EQ(state.count, 3U);
  expect_event(state, 0, Register);
  expect_event(state, 1, Duplicate, SOURCE_FD);
  expect_event(state, 2, Unregister, PERSONALITY);
  expect_error(owner.capture(SOURCE_FD), EBUSY);
  EXPECT_EQ(state.count, 3U);
  EXPECT_EQ(owner.error(), EMFILE);
  EXPECT_EQ(owner.cleanup_error(), EIO);
  state.unregister_error = EINTR;
  expect_error(owner.release(), EINTR);
  expect_unready(owner, false);
  EXPECT_EQ(owner.error(), EMFILE);
  EXPECT_EQ(owner.cleanup_error(), EINTR);
  expect_event(state, 3, Unregister, PERSONALITY);
  state.unregister_error = 0;
  EXPECT_TRUE(owner.release().has_value());
  expect_unready(owner, true);
  EXPECT_EQ(owner.error(), EMFILE);
  EXPECT_EQ(owner.cleanup_error(), 0);
  expect_event(state, 4, Unregister, PERSONALITY);
  EXPECT_TRUE(owner.release().has_value());
  EXPECT_EQ(state.count, 5U);
  state.duplicate_error = 0;
  EXPECT_TRUE(owner.capture(SOURCE_FD).has_value());
  EXPECT_EQ(owner.error(), 0);
  EXPECT_TRUE(owner.release().has_value());
  EXPECT_EQ(state.count, 9U);
  errno_guard.check();
}

TEST(LlvmLibcAioSubmissionResourcesTest, ReleaseRetriesOnlyOwnedPersonality) {
  // Cross both failures: close remains consumed while unregister is retryable.
  const int close_errors[] = {0, EINTR, EIO, EBADF};
  const int unregister_errors[] = {0, EINTR, EIO, ENOMEM};
  for (int close_error : close_errors) {
    for (int unregister_error : unregister_errors) {
      ErrnoGuard errno_guard;
      State state;
      state.duplicate_fd = 0;
      FakeRing ring{state};
      {
        Owner owner(ring, FakeOperations{state});
        observe(state, owner);
        ASSERT_TRUE(owner.capture(SOURCE_FD).has_value());
        state.close_error = close_error;
        state.unregister_error = unregister_error;
        auto released = owner.release();
        int first_error = close_error ? close_error : unregister_error;
        if (first_error)
          expect_error(released, first_error);
        else
          EXPECT_TRUE(released.has_value());
        EXPECT_EQ(owner.error(), 0);
        EXPECT_EQ(owner.cleanup_error(), first_error);
        expect_unready(owner, unregister_error == 0);
        EXPECT_FALSE(state.fd_owned);
        EXPECT_TRUE(state.fd_number_reused);
        EXPECT_EQ(state.count, 4U);
        expect_event(state, 2, Close, 0);
        expect_event(state, 3, Unregister, PERSONALITY);
        if (unregister_error) {
          expect_error(owner.capture(SOURCE_FD), EBUSY);
          EXPECT_EQ(state.count, 4U);
          expect_error(owner.release(), unregister_error);
          EXPECT_EQ(owner.cleanup_error(), unregister_error);
          expect_event(state, 4, Unregister, PERSONALITY);
          state.unregister_error = 0;
        }
        EXPECT_TRUE(owner.release().has_value());
        expect_unready(owner, true);
        if (unregister_error)
          expect_event(state, 5, Unregister, PERSONALITY);
        EXPECT_TRUE(owner.release().has_value());
        errno_guard.check();
      }
      EXPECT_EQ(state.count, unregister_error ? 6U : 4U);
      EXPECT_TRUE(state.fd_number_reused);
    }
  }
}

TEST(LlvmLibcAioSubmissionResourcesTest, UndrainedDestructionTraps) {
  EXPECT_DEATH(
      [] {
        State state;
        FakeRing ring{state};
        Owner owner(ring, FakeOperations{state});
        // Cleanup would succeed, but destruction must not perform it.
        (void)owner.capture(SOURCE_FD);
      },
      WITH_SIGNAL(-1));
  EXPECT_DEATH(
      [] {
        State state;
        state.duplicate_error = EMFILE;
        state.unregister_error = EIO;
        FakeRing ring{state};
        Owner owner(ring, FakeOperations{state});
        (void)owner.capture(SOURCE_FD);
        // Even a recoverable rollback must be drained explicitly.
        state.unregister_error = 0;
      },
      WITH_SIGNAL(-1));
}

TEST(LlvmLibcAioSubmissionResourcesTest,
     NativeOperationsPreserveErrnoOnErrors) {
  ErrnoGuard errno_guard;
  LinuxOperations operations;
  expect_error(operations.duplicate(-1), EBADF);
  expect_error(operations.close(-1), EBADF);
  State state;
  FakeRing ring{state};
  LIBC_NAMESPACE::AioSubmissionResources<FakeRing> owner(ring);
  expect_error(owner.capture(-1), EBADF);
  EXPECT_FALSE(owner.ready());
  EXPECT_TRUE(owner.drained());
  EXPECT_EQ(owner.error(), EBADF);
  EXPECT_EQ(owner.cleanup_error(), 0);
  EXPECT_EQ(state.count, 2U);
  expect_event(state, 0, Register);
  expect_event(state, 1, Unregister, PERSONALITY);
  EXPECT_TRUE(owner.release().has_value());
  errno_guard.check();
}

TEST(LlvmLibcAioSubmissionResourcesTest,
     NativeDuplicateHoldsCloexecDescriptor) {
  ErrnoGuard errno_guard;
  auto opened = LIBC_NAMESPACE::linux_syscalls::syscall_checked<int>(
      SYS_openat, AT_FDCWD, "/dev/null", O_RDONLY | O_CLOEXEC, 0);
  ASSERT_TRUE(opened.has_value());
  State state;
  FakeRing ring{state};
  LIBC_NAMESPACE::AioSubmissionResources<FakeRing> owner(ring);
  auto captured = owner.capture(*opened);
  // Close the application's descriptor independently of capture success.
  EXPECT_TRUE(LinuxOperations{}.close(*opened).has_value());
  ASSERT_TRUE(captured.has_value());
  EXPECT_TRUE(owner.ready());
  EXPECT_NE(owner.descriptor(), *opened);
#ifdef SYS_fcntl
  constexpr long NUMBER = SYS_fcntl;
#else
  constexpr long NUMBER = SYS_fcntl64;
#endif
  auto flags = LIBC_NAMESPACE::linux_syscalls::syscall_checked<int>(
      NUMBER, owner.descriptor(), F_GETFD, 0);
  EXPECT_TRUE(flags.has_value());
  if (flags)
    EXPECT_NE(*flags & FD_CLOEXEC, 0);
  char byte;
  auto read = LIBC_NAMESPACE::linux_syscalls::syscall_checked<int>(
      SYS_read, owner.descriptor(), &byte, 1);
  EXPECT_TRUE(read.has_value());
  if (read)
    EXPECT_EQ(*read, 0);
  EXPECT_TRUE(owner.release().has_value());
  EXPECT_TRUE(owner.drained());
  EXPECT_EQ(state.count, 2U);
  expect_event(state, 0, Register);
  expect_event(state, 1, Unregister, PERSONALITY);
  errno_guard.check();
}
