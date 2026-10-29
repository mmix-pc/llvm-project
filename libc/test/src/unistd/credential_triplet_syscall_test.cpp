//===-- Credential triplet syscall injection tests ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/limits_macros.h"
#include "hdr/types/gid_t.h"
#include "hdr/types/uid_t.h"
#include "src/__support/CPP/type_traits/is_same.h"
#include "src/__support/OSUtil/syscall.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"
#include <sys/syscall.h>

namespace {
struct {
  int result, calls;
  long number;
  void *pointers[3];
  unsigned long long ids[3];
} state;

void reset(int result = 0, int saved_errno = EDOM) {
  state = {};
  state.result = result;
  LIBC_NAMESPACE::libc_errno = saved_errno;
}

#if defined(SYS_getresuid32)
constexpr long GETRESUID_NUMBER = SYS_getresuid32;
#else
constexpr long GETRESUID_NUMBER = SYS_getresuid;
#endif
#if defined(SYS_getresgid32)
constexpr long GETRESGID_NUMBER = SYS_getresgid32;
#else
constexpr long GETRESGID_NUMBER = SYS_getresgid;
#endif
#if defined(SYS_setresuid32)
constexpr long SETRESUID_NUMBER = SYS_setresuid32;
#else
constexpr long SETRESUID_NUMBER = SYS_setresuid;
#endif
#if defined(SYS_setresgid32)
constexpr long SETRESGID_NUMBER = SYS_setresgid32;
#else
constexpr long SETRESGID_NUMBER = SYS_setresgid;
#endif
} // namespace

namespace LIBC_NAMESPACE_DECL {
template <typename R, typename Id>
R credential_triplet_test_syscall(long number, Id *real, Id *effective,
                                  Id *saved) {
  static_assert(cpp::is_same_v<Id, uid_t> || cpp::is_same_v<Id, gid_t>);
  ++state.calls;
  state.number = number;
  state.pointers[0] = real;
  state.pointers[1] = effective;
  state.pointers[2] = saved;
  if (state.result == 0 && real && effective && saved) {
    *real = Id(0x10001U);
    *effective = Id(0x80000002U);
    *saved = Id(UINT_MAX);
  }
  return static_cast<R>(state.result);
}

template <typename R, typename Id>
R credential_triplet_test_syscall(long number, Id real, Id effective,
                                  Id saved) {
  static_assert(cpp::is_same_v<Id, uid_t> || cpp::is_same_v<Id, gid_t>);
  ++state.calls;
  state.number = number;
  state.ids[0] = real;
  state.ids[1] = effective;
  state.ids[2] = saved;
  return static_cast<R>(state.result);
}
} // namespace LIBC_NAMESPACE_DECL

// Compile the production sources with private entrypoints and a fake syscall;
// no credential syscall can reach the host, even in the setter tests.
#define syscall_impl credential_triplet_test_syscall
#define getresuid injected_getresuid
#define getresgid injected_getresgid
#define setresuid injected_setresuid
#define setresgid injected_setresgid
#include "src/unistd/linux/getresgid.cpp"
#include "src/unistd/linux/getresuid.cpp"
#include "src/unistd/linux/setresgid.cpp"
#include "src/unistd/linux/setresuid.cpp"
#undef setresgid
#undef setresuid
#undef getresgid
#undef getresuid
#undef syscall_impl

namespace {
template <typename Id>
void checkPointers(Id *real, Id *effective, Id *saved, long number) {
  EXPECT_EQ(state.calls, 1);
  EXPECT_EQ(state.number, number);
  EXPECT_EQ(state.pointers[0], static_cast<void *>(real));
  EXPECT_EQ(state.pointers[1], static_cast<void *>(effective));
  EXPECT_EQ(state.pointers[2], static_cast<void *>(saved));
}

template <typename Id>
void checkIds(Id real, Id effective, Id saved, long number) {
  EXPECT_EQ(state.calls, 1);
  EXPECT_EQ(state.number, number);
  EXPECT_EQ(state.ids[0], static_cast<unsigned long long>(real));
  EXPECT_EQ(state.ids[1], static_cast<unsigned long long>(effective));
  EXPECT_EQ(state.ids[2], static_cast<unsigned long long>(saved));
}

template <typename Id>
void checkGetterSuccess(int (*get)(Id *, Id *, Id *), long number) {
  const int saved_errnos[] = {0, EDOM};
  for (int saved_errno : saved_errnos) {
    Id ids[] = {Id(7), Id(8), Id(9)};
    reset(0, saved_errno);
    EXPECT_EQ(get(&ids[0], &ids[1], &ids[2]), 0);
    checkPointers(&ids[0], &ids[1], &ids[2], number);
    EXPECT_EQ(ids[0], Id(0x10001U));
    EXPECT_EQ(ids[1], Id(0x80000002U));
    EXPECT_EQ(ids[2], Id(UINT_MAX));
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), saved_errno);

    reset(0, saved_errno);
    EXPECT_EQ(get(&ids[0], &ids[0], &ids[0]), 0);
    checkPointers(&ids[0], &ids[0], &ids[0], number);
    EXPECT_EQ(ids[0], Id(UINT_MAX));
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), saved_errno);
  }
}

template <typename Id>
void checkGetterErrors(int (*get)(Id *, Id *, Id *), long number) {
  const int errors[] = {EFAULT, EINVAL, EPERM, EINTR, EIO, ENOSYS};
  for (int error : errors) {
    Id ids[] = {Id(7), Id(8), Id(9)};
    reset(-error);
    EXPECT_EQ(get(&ids[0], &ids[1], &ids[2]), -1);
    checkPointers(&ids[0], &ids[1], &ids[2], number);
    EXPECT_EQ(ids[0], Id(7));
    EXPECT_EQ(ids[1], Id(8));
    EXPECT_EQ(ids[2], Id(9));
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
  }

  // Every null-pointer combination must be passed through to the syscall.
  for (unsigned mask = 1; mask < 8; ++mask) {
    Id ids[] = {Id(7), Id(8), Id(9)};
    Id *real = mask & 1 ? nullptr : &ids[0];
    Id *effective = mask & 2 ? nullptr : &ids[1];
    Id *saved = mask & 4 ? nullptr : &ids[2];
    reset(-EFAULT);
    EXPECT_EQ(get(real, effective, saved), -1);
    checkPointers(real, effective, saved, number);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), EFAULT);
  }
}

template <typename Id>
void checkSetterSuccess(int (*set)(Id, Id, Id), long number) {
  const Id ids[] = {Id(0),           Id(1),        Id(0x10001U),
                    Id(0x80000002U), Id(UINT_MAX), Id(-1)};
  const int saved_errnos[] = {0, EDOM};
  for (int saved_errno : saved_errnos) {
    for (Id real : ids) {
      for (Id effective : ids) {
        for (Id saved : ids) {
          reset(0, saved_errno);
          EXPECT_EQ(set(real, effective, saved), 0);
          checkIds(real, effective, saved, number);
          EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), saved_errno);
        }
      }
    }
  }
}

template <typename Id>
void checkSetterErrors(int (*set)(Id, Id, Id), long number) {
  const int errors[] = {EPERM, EINVAL, EAGAIN, EINTR, EIO, ENOSYS};
  for (int error : errors) {
    reset(-error);
    EXPECT_EQ(set(Id(-1), Id(0x80000002U), Id(UINT_MAX)), -1);
    checkIds(Id(-1), Id(0x80000002U), Id(UINT_MAX), number);
    EXPECT_EQ(int(LIBC_NAMESPACE::libc_errno), error);
  }
}
} // namespace

TEST(LlvmLibcCredentialTripletSyscallTest,
     GetForwardsPointersAndFullWidthOutputsWithoutChangingErrno) {
  checkGetterSuccess(LIBC_NAMESPACE::injected_getresuid, GETRESUID_NUMBER);
  checkGetterSuccess(LIBC_NAMESPACE::injected_getresgid, GETRESGID_NUMBER);
}

TEST(LlvmLibcCredentialTripletSyscallTest, GetPropagatesErrorsWithoutRetry) {
  checkGetterErrors(LIBC_NAMESPACE::injected_getresuid, GETRESUID_NUMBER);
  checkGetterErrors(LIBC_NAMESPACE::injected_getresgid, GETRESGID_NUMBER);
}

TEST(LlvmLibcCredentialTripletSyscallTest,
     SetForwardsThreeFullWidthIdsAndSentinelsWithoutChangingErrno) {
  checkSetterSuccess(LIBC_NAMESPACE::injected_setresuid, SETRESUID_NUMBER);
  checkSetterSuccess(LIBC_NAMESPACE::injected_setresgid, SETRESGID_NUMBER);
}

TEST(LlvmLibcCredentialTripletSyscallTest, SetPropagatesErrorsWithoutRetry) {
  checkSetterErrors(LIBC_NAMESPACE::injected_setresuid, SETRESUID_NUMBER);
  checkSetterErrors(LIBC_NAMESPACE::injected_setresgid, SETRESGID_NUMBER);
}
