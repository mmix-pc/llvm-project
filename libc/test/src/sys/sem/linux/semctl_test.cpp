//===-- Tests for semctl argument handling --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/sys_ipc_macros.h"
#include "hdr/sys_sem_macros.h"
#include "hdr/types/struct_semid_ds.h"
#include "hdr/types/struct_seminfo.h"
#include "src/__support/libc_errno.h"
#include "src/sys/sem/semctl.h"
#include "src/sys/sem/semget.h"
#include "src/unistd/getpid.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/ErrnoSetterMatcher.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::Fails;
using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::GE;
using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::returns;
using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::Succeeds;

namespace {

// Deliberately application-defined, independently of the provider's type.
union semun {
  int val;
  struct semid_ds *buf;
  unsigned short *array;
  struct seminfo *__buf;
};

class LlvmLibcSemctlTest : public LIBC_NAMESPACE::testing::ErrnoCheckingTest {
protected:
  int semid = -1;

  int createSet() {
    semid = LIBC_NAMESPACE::semget(IPC_PRIVATE, 2, IPC_CREAT | 0600);
    return semid;
  }

  void TearDown() override {
    if (semid >= 0)
      EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 0, IPC_RMID), Succeeds());
    LIBC_NAMESPACE::testing::ErrnoCheckingTest::TearDown();
  }
};

} // namespace

TEST_F(LlvmLibcSemctlTest, SetValWithOnlyValueInitialized) {
  ASSERT_THAT(createSet(), returns(GE(0)).with_errno(0));

  const int values[] = {0, 1, 0x1234, 32767};
  for (int value : values) {
    // The caller need not initialize bytes outside the active int member.
    union semun arg;
    arg.val = value;
    libc_errno = EDOM;
    ASSERT_THAT(LIBC_NAMESPACE::semctl(semid, 1, SETVAL, arg), Succeeds(0, EDOM));
    EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 1, GETVAL), Succeeds(value));
    EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 0, GETVAL), Succeeds(0));
  }
}

TEST_F(LlvmLibcSemctlTest, SetValWithNonzeroUnusedBytes) {
  ASSERT_THAT(createSet(), returns(GE(0)).with_errno(0));

  union semun arg;
  auto *bytes = reinterpret_cast<unsigned char *>(&arg);
  const unsigned char fills[] = {0x55, 0xaa};
  for (unsigned char fill : fills) {
    for (unsigned i = 0; i < sizeof(arg); ++i)
      bytes[i] = fill;
    arg.val = 0x2345;
    ASSERT_THAT(LIBC_NAMESPACE::semctl(semid, 0, SETVAL, arg), Succeeds());
    EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 0, GETVAL), Succeeds(0x2345));
  }
}

TEST_F(LlvmLibcSemctlTest, SetValErrorsPreserveValue) {
  ASSERT_THAT(createSet(), returns(GE(0)).with_errno(0));
  union semun arg;
  arg.val = 7;
  ASSERT_THAT(LIBC_NAMESPACE::semctl(semid, 0, SETVAL, arg), Succeeds());

  const int invalid_values[] = {-1, 32768};
  for (int value : invalid_values) {
    arg.val = value;
    EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 0, SETVAL, arg), Fails(ERANGE));
    EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 0, GETVAL), Succeeds(7));
  }
  arg.val = 1;
  EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 2, SETVAL, arg), Fails(EINVAL));
  EXPECT_THAT(LIBC_NAMESPACE::semctl(-1, 0, SETVAL, arg), Fails(EINVAL));
}

TEST_F(LlvmLibcSemctlTest, ArrayCommands) {
  ASSERT_THAT(createSet(), returns(GE(0)).with_errno(0));
  unsigned short values[] = {3, 0x1234};
  union semun arg;
  arg.array = values;
  ASSERT_THAT(LIBC_NAMESPACE::semctl(semid, 0, SETALL, arg), Succeeds());
  EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 0, GETVAL), Succeeds(3));
  EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 1, GETVAL), Succeeds(0x1234));

  unsigned short received[] = {0, 0};
  arg.array = received;
  libc_errno = EDOM;
  ASSERT_THAT(LIBC_NAMESPACE::semctl(semid, 0, GETALL, arg), Succeeds(0, EDOM));
  EXPECT_EQ(received[0], values[0]);
  EXPECT_EQ(received[1], values[1]);
}

TEST_F(LlvmLibcSemctlTest, StatAndSetCommands) {
  ASSERT_THAT(createSet(), returns(GE(0)).with_errno(0));
  struct semid_ds status{};
  union semun arg;
  arg.buf = &status;
  ASSERT_THAT(LIBC_NAMESPACE::semctl(semid, 0, IPC_STAT, arg), Succeeds());
  EXPECT_EQ(status.sem_nsems, decltype(status.sem_nsems)(2));
  EXPECT_EQ(static_cast<unsigned>(status.sem_perm.mode) & 0777U, 0600U);

  status.sem_perm.mode = 0640;
  ASSERT_THAT(LIBC_NAMESPACE::semctl(semid, 0, IPC_SET, arg), Succeeds());
  status = {};
  ASSERT_THAT(LIBC_NAMESPACE::semctl(semid, 0, IPC_STAT, arg), Succeeds());
  EXPECT_EQ(status.sem_nsems, decltype(status.sem_nsems)(2));
  EXPECT_EQ(static_cast<unsigned>(status.sem_perm.mode) & 0777U, 0640U);
}

TEST_F(LlvmLibcSemctlTest, InfoCommands) {
  ASSERT_THAT(createSet(), returns(GE(0)).with_errno(0));
  const int commands[] = {IPC_INFO, SEM_INFO};
  for (int command : commands) {
    struct seminfo info{};
    union semun arg;
    arg.__buf = &info;
    ASSERT_THAT(LIBC_NAMESPACE::semctl(semid, 0, command, arg),
                returns(GE(0)).with_errno(0));
    EXPECT_GE(info.semmsl, 2);
    EXPECT_EQ(info.semvmx, 32767);
  }
}

TEST_F(LlvmLibcSemctlTest, NullPointerMembers) {
  ASSERT_THAT(createSet(), returns(GE(0)).with_errno(0));
  union semun arg;
  arg.buf = nullptr;
  EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 0, IPC_STAT, arg), Fails(EFAULT));
  EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 0, IPC_SET, arg), Fails(EFAULT));
  arg.array = nullptr;
  EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 0, GETALL, arg), Fails(EFAULT));
  EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 0, SETALL, arg), Fails(EFAULT));
  arg.__buf = nullptr;
  EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 0, IPC_INFO, arg), Fails(EFAULT));
  EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 0, SEM_INFO, arg), Fails(EFAULT));
}

TEST_F(LlvmLibcSemctlTest, CommandsWithoutFourthArgument) {
  ASSERT_THAT(createSet(), returns(GE(0)).with_errno(0));
  union semun arg;
  arg.val = 1;
  ASSERT_THAT(LIBC_NAMESPACE::semctl(semid, 0, SETVAL, arg), Succeeds());

  libc_errno = EDOM;
  EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 0, GETVAL), Succeeds(1, EDOM));
  EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 0, GETPID),
              Succeeds(static_cast<int>(LIBC_NAMESPACE::getpid())));
  EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 0, GETNCNT), Succeeds(0));
  EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 0, GETZCNT), Succeeds(0));
  EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 0, -1), Fails(EINVAL));

  ASSERT_THAT(LIBC_NAMESPACE::semctl(semid, 0, IPC_RMID), Succeeds());
  semid = -1;
  EXPECT_THAT(LIBC_NAMESPACE::semctl(semid, 0, GETVAL), Fails(EINVAL));
}
