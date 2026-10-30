//===-- Tests for SysV shared memory --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/sys_ipc_macros.h"
#include "hdr/sys_shm_macros.h"
#include "hdr/types/struct_shmid_ds.h"
#include "src/sys/shm/shmat.h"
#include "src/sys/shm/shmctl.h"
#include "src/sys/shm/shmdt.h"
#include "src/sys/shm/shmget.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/ErrnoSetterMatcher.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::Fails;
using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::Succeeds;

namespace {

// Keep ownership in the fixture so failed assertions do not leak kernel IPC
// objects or mappings. A null address is not the shmat failure sentinel.
class LlvmLibcSysShmTest : public LIBC_NAMESPACE::testing::ErrnoCheckingTest {
protected:
  int shmid = -1;
  void *writable = reinterpret_cast<void *>(-1);
  void *readonly = reinterpret_cast<void *>(-1);
  static constexpr size_t SIZE = 128;

  void TearDown() override {
    if (shmid >= 0)
      EXPECT_THAT(LIBC_NAMESPACE::shmctl(shmid, IPC_RMID, nullptr), Succeeds());
    if (readonly != reinterpret_cast<void *>(-1))
      EXPECT_THAT(LIBC_NAMESPACE::shmdt(readonly), Succeeds());
    if (writable != reinterpret_cast<void *>(-1))
      EXPECT_THAT(LIBC_NAMESPACE::shmdt(writable), Succeeds());
    LIBC_NAMESPACE::testing::ErrnoCheckingTest::TearDown();
  }
};

} // namespace

TEST_F(LlvmLibcSysShmTest, AttachShareAndRemove) {
  libc_errno = EDOM;
  shmid = LIBC_NAMESPACE::shmget(IPC_PRIVATE, SIZE, IPC_CREAT | 0600);
  ASSERT_GE(shmid, 0);
  ASSERT_ERRNO_EQ(EDOM);

  struct shmid_ds info = {};
  libc_errno = EDOM;
  ASSERT_THAT(LIBC_NAMESPACE::shmctl(shmid, IPC_STAT, &info),
              Succeeds(0, EDOM));
  EXPECT_EQ(info.shm_segsz, SIZE);
  EXPECT_EQ(info.shm_nattch, static_cast<decltype(info.shm_nattch)>(0));
  EXPECT_EQ(static_cast<unsigned>(info.shm_perm.mode) & 0777U, 0600U);

  info.shm_perm.mode = 0640;
  ASSERT_THAT(LIBC_NAMESPACE::shmctl(shmid, IPC_SET, &info), Succeeds());
  ASSERT_THAT(LIBC_NAMESPACE::shmctl(shmid, IPC_STAT, &info), Succeeds());
  EXPECT_EQ(static_cast<unsigned>(info.shm_perm.mode) & 0777U, 0640U);

  libc_errno = EDOM;
  writable = LIBC_NAMESPACE::shmat(shmid, nullptr, 0);
  ASSERT_NE(writable, reinterpret_cast<void *>(-1));
  ASSERT_ERRNO_EQ(EDOM);
  readonly = LIBC_NAMESPACE::shmat(shmid, nullptr, SHM_RDONLY);
  ASSERT_NE(readonly, reinterpret_cast<void *>(-1));
  ASSERT_ERRNO_SUCCESS();
  ASSERT_THAT(LIBC_NAMESPACE::shmctl(shmid, IPC_STAT, &info), Succeeds());
  EXPECT_EQ(info.shm_nattch, static_cast<decltype(info.shm_nattch)>(2));

  auto *write_bytes = static_cast<volatile unsigned char *>(writable);
  auto *read_bytes = static_cast<const volatile unsigned char *>(readonly);
  for (size_t i = 0; i < SIZE; ++i) {
    EXPECT_EQ(read_bytes[i], static_cast<unsigned char>(0));
    write_bytes[i] = static_cast<unsigned char>(i + 1);
    EXPECT_EQ(read_bytes[i], static_cast<unsigned char>(i + 1));
  }

  ASSERT_THAT(LIBC_NAMESPACE::shmctl(shmid, IPC_RMID, nullptr), Succeeds());
  shmid = -1;
  // Removal is deferred until the last mapping is detached.
  write_bytes[0] = 42;
  EXPECT_EQ(read_bytes[0], static_cast<unsigned char>(42));
  ASSERT_THAT(LIBC_NAMESPACE::shmdt(readonly), Succeeds());
  readonly = reinterpret_cast<void *>(-1);
  libc_errno = EDOM;
  ASSERT_THAT(LIBC_NAMESPACE::shmdt(writable), Succeeds(0, EDOM));
  writable = reinterpret_cast<void *>(-1);
}

TEST_F(LlvmLibcSysShmTest, InvalidArguments) {
  EXPECT_THAT(LIBC_NAMESPACE::shmget(IPC_PRIVATE, 0, IPC_CREAT | 0600),
              Fails(EINVAL));
  EXPECT_THAT(LIBC_NAMESPACE::shmat(-1, nullptr, 0),
              Fails(EINVAL, reinterpret_cast<void *>(-1)));
  EXPECT_THAT(LIBC_NAMESPACE::shmdt(reinterpret_cast<void *>(1)),
              Fails(EINVAL));
  struct shmid_ds info = {};
  EXPECT_THAT(LIBC_NAMESPACE::shmctl(-1, IPC_STAT, &info), Fails(EINVAL));
}

TEST_F(LlvmLibcSysShmTest, InvalidControlAndBuffer) {
  shmid = LIBC_NAMESPACE::shmget(IPC_PRIVATE, SIZE, IPC_CREAT | 0600);
  ASSERT_GE(shmid, 0);
  ASSERT_ERRNO_SUCCESS();
  EXPECT_THAT(LIBC_NAMESPACE::shmat(shmid, reinterpret_cast<void *>(1), 0),
              Fails(EINVAL, reinterpret_cast<void *>(-1)));
  EXPECT_THAT(LIBC_NAMESPACE::shmctl(shmid, IPC_STAT, nullptr), Fails(EFAULT));
  EXPECT_THAT(LIBC_NAMESPACE::shmctl(shmid, IPC_SET, nullptr), Fails(EFAULT));
  EXPECT_THAT(LIBC_NAMESPACE::shmctl(shmid, -1, nullptr), Fails(EINVAL));
}
