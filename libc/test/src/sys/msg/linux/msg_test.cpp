//===-- Tests for SysV message queues -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/sys_ipc_macros.h"
#include "hdr/sys_msg_macros.h"
#include "hdr/types/struct_msqid_ds.h"
#include "src/sys/msg/msgctl.h"
#include "src/sys/msg/msgget.h"
#include "src/sys/msg/msgrcv.h"
#include "src/sys/msg/msgsnd.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/ErrnoSetterMatcher.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::Fails;
using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::Succeeds;

namespace {

struct Message {
  long type;
  char text[4];
};

// Queue ownership survives failed assertions, and every transfer is
// nonblocking.
class LlvmLibcSysMsgTest : public LIBC_NAMESPACE::testing::ErrnoCheckingTest {
protected:
  int msqid = -1;

  void TearDown() override {
    if (msqid >= 0)
      EXPECT_THAT(LIBC_NAMESPACE::msgctl(msqid, IPC_RMID, nullptr), Succeeds());
    LIBC_NAMESPACE::testing::ErrnoCheckingTest::TearDown();
  }
};

} // namespace

TEST_F(LlvmLibcSysMsgTest, SendReceiveAndControl) {
  libc_errno = EDOM;
  msqid = LIBC_NAMESPACE::msgget(IPC_PRIVATE, IPC_CREAT | 0600);
  ASSERT_GE(msqid, 0);
  ASSERT_ERRNO_EQ(EDOM);

  Message sent = {7, {'a', '\0', 'c', 'd'}};
  libc_errno = EDOM;
  ASSERT_THAT(
      LIBC_NAMESPACE::msgsnd(msqid, &sent, sizeof(sent.text), IPC_NOWAIT),
      Succeeds(0, EDOM));

  struct msqid_ds info = {};
  libc_errno = EDOM;
  ASSERT_THAT(LIBC_NAMESPACE::msgctl(msqid, IPC_STAT, &info),
              Succeeds(0, EDOM));
  EXPECT_EQ(info.msg_qnum, static_cast<decltype(info.msg_qnum)>(1));
  EXPECT_EQ(static_cast<unsigned>(info.msg_perm.mode) & 0777U, 0600U);
  info.msg_perm.mode = 0640;
  ASSERT_THAT(LIBC_NAMESPACE::msgctl(msqid, IPC_SET, &info), Succeeds());
  ASSERT_THAT(LIBC_NAMESPACE::msgctl(msqid, IPC_STAT, &info), Succeeds());
  EXPECT_EQ(static_cast<unsigned>(info.msg_perm.mode) & 0777U, 0640U);

  Message received = {};
  libc_errno = EDOM;
  ASSERT_THAT(LIBC_NAMESPACE::msgrcv(msqid, &received, sizeof(received.text), 7,
                                     IPC_NOWAIT),
              Succeeds<ssize_t>(sizeof(sent.text), EDOM));
  EXPECT_EQ(received.type, sent.type);
  for (size_t i = 0; i < sizeof(sent.text); ++i)
    EXPECT_EQ(received.text[i], sent.text[i]);
  ASSERT_THAT(LIBC_NAMESPACE::msgctl(msqid, IPC_STAT, &info), Succeeds());
  EXPECT_EQ(info.msg_qnum, static_cast<decltype(info.msg_qnum)>(0));
  EXPECT_THAT(LIBC_NAMESPACE::msgrcv(msqid, &received, sizeof(received.text), 0,
                                     IPC_NOWAIT),
              Fails<ssize_t>(ENOMSG));

  ASSERT_THAT(LIBC_NAMESPACE::msgctl(msqid, IPC_RMID, nullptr), Succeeds());
  msqid = -1;
}

TEST_F(LlvmLibcSysMsgTest, SelectionTruncationAndZeroLength) {
  msqid = LIBC_NAMESPACE::msgget(IPC_PRIVATE, IPC_CREAT | 0600);
  ASSERT_GE(msqid, 0);
  ASSERT_ERRNO_SUCCESS();
  Message high = {5, {'h', 'i', 'g', 'h'}};
  Message low = {2, {'l', 'o', 'w', '!'}};
  ASSERT_THAT(
      LIBC_NAMESPACE::msgsnd(msqid, &high, sizeof(high.text), IPC_NOWAIT),
      Succeeds());
  ASSERT_THAT(LIBC_NAMESPACE::msgsnd(msqid, &low, sizeof(low.text), IPC_NOWAIT),
              Succeeds());

  Message received = {0, {'x', 'x', 'x', 'x'}};
  EXPECT_THAT(LIBC_NAMESPACE::msgrcv(msqid, &received, sizeof(received.text), 9,
                                     IPC_NOWAIT),
              Fails<ssize_t>(ENOMSG));
  // Negative selection chooses the lowest eligible type, not the queue head.
  EXPECT_THAT(LIBC_NAMESPACE::msgrcv(msqid, &received, 2, -5, IPC_NOWAIT),
              Fails<ssize_t>(E2BIG));
  ASSERT_THAT(
      LIBC_NAMESPACE::msgrcv(msqid, &received, 2, -5, IPC_NOWAIT | MSG_NOERROR),
      Succeeds<ssize_t>(2));
  EXPECT_EQ(received.type, 2L);
  EXPECT_EQ(received.text[0], 'l');
  EXPECT_EQ(received.text[1], 'o');
  EXPECT_EQ(received.text[2], 'x');
  EXPECT_EQ(received.text[3], 'x');

  ASSERT_THAT(LIBC_NAMESPACE::msgrcv(msqid, &received, sizeof(received.text), 0,
                                     IPC_NOWAIT),
              Succeeds<ssize_t>(sizeof(high.text)));
  EXPECT_EQ(received.type, 5L);
  for (size_t i = 0; i < sizeof(high.text); ++i)
    EXPECT_EQ(received.text[i], high.text[i]);

  ASSERT_THAT(LIBC_NAMESPACE::msgsnd(msqid, &low, 0, IPC_NOWAIT), Succeeds());
  received.type = 0;
  ASSERT_THAT(LIBC_NAMESPACE::msgrcv(msqid, &received, 0, 0, IPC_NOWAIT),
              Succeeds<ssize_t>(0));
  EXPECT_EQ(received.type, low.type);
}

TEST_F(LlvmLibcSysMsgTest, FullQueue) {
  msqid = LIBC_NAMESPACE::msgget(IPC_PRIVATE, IPC_CREAT | 0600);
  ASSERT_GE(msqid, 0);
  ASSERT_ERRNO_SUCCESS();
  struct msqid_ds info = {};
  ASSERT_THAT(LIBC_NAMESPACE::msgctl(msqid, IPC_STAT, &info), Succeeds());
  // Lowering the limit requires no privilege and makes EAGAIN deterministic.
  info.msg_qbytes = 1;
  ASSERT_THAT(LIBC_NAMESPACE::msgctl(msqid, IPC_SET, &info), Succeeds());
  Message message = {1, {'a'}};
  ASSERT_THAT(LIBC_NAMESPACE::msgsnd(msqid, &message, 1, IPC_NOWAIT),
              Succeeds());
  EXPECT_THAT(LIBC_NAMESPACE::msgsnd(msqid, &message, 1, IPC_NOWAIT),
              Fails(EAGAIN));
}

TEST_F(LlvmLibcSysMsgTest, InvalidArguments) {
  Message message = {1, {'a'}};
  struct msqid_ds info = {};
  EXPECT_THAT(LIBC_NAMESPACE::msgsnd(-1, &message, 1, IPC_NOWAIT),
              Fails(EINVAL));
  EXPECT_THAT(LIBC_NAMESPACE::msgrcv(-1, &message, 1, 0, IPC_NOWAIT),
              Fails<ssize_t>(EINVAL));
  EXPECT_THAT(LIBC_NAMESPACE::msgctl(-1, IPC_STAT, &info), Fails(EINVAL));

  msqid = LIBC_NAMESPACE::msgget(IPC_PRIVATE, IPC_CREAT | 0600);
  ASSERT_GE(msqid, 0);
  ASSERT_ERRNO_SUCCESS();
  EXPECT_THAT(LIBC_NAMESPACE::msgctl(msqid, IPC_STAT, nullptr), Fails(EFAULT));
  EXPECT_THAT(LIBC_NAMESPACE::msgctl(msqid, IPC_SET, nullptr), Fails(EFAULT));
  EXPECT_THAT(LIBC_NAMESPACE::msgctl(msqid, -1, nullptr), Fails(EINVAL));
  EXPECT_THAT(LIBC_NAMESPACE::msgsnd(msqid, nullptr, 1, IPC_NOWAIT),
              Fails(EFAULT));
  message.type = 0;
  EXPECT_THAT(LIBC_NAMESPACE::msgsnd(msqid, &message, 1, IPC_NOWAIT),
              Fails(EINVAL));
  message.type = -1;
  EXPECT_THAT(LIBC_NAMESPACE::msgsnd(msqid, &message, 1, IPC_NOWAIT),
              Fails(EINVAL));

  message.type = 1;
  ASSERT_THAT(LIBC_NAMESPACE::msgsnd(msqid, &message, 1, IPC_NOWAIT),
              Succeeds());
  EXPECT_THAT(LIBC_NAMESPACE::msgrcv(msqid, nullptr, 1, 0, IPC_NOWAIT),
              Fails<ssize_t>(EFAULT));
}
