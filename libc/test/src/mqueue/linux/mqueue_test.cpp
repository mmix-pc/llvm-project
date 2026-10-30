//===-- Tests for Linux POSIX message queues ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/types/mode_t.h"
#include "src/fcntl/fcntl.h"
#include "src/mqueue/mq_close.h"
#include "src/mqueue/mq_getattr.h"
#include "src/mqueue/mq_open.h"
#include "src/mqueue/mq_receive.h"
#include "src/mqueue/mq_send.h"
#include "src/mqueue/mq_setattr.h"
#include "src/mqueue/mq_timedreceive.h"
#include "src/mqueue/mq_timedsend.h"
#include "src/mqueue/mq_unlink.h"
#include "src/stdio/snprintf.h"
#include "src/unistd/dup.h"
#include "src/unistd/getpid.h"
#include "test/UnitTest/ErrnoCheckingTest.h"
#include "test/UnitTest/ErrnoSetterMatcher.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::Fails;
using LIBC_NAMESPACE::testing::ErrnoSetterMatcher::Succeeds;

namespace {

class LlvmLibcMqueueTest : public LIBC_NAMESPACE::testing::ErrnoCheckingTest {
protected:
  char name[96] = {};
  mqd_t mqdes = -1;
  mqd_t other = -1;
  mqd_t duplicate = -1;
  bool linked = false;

  mqd_t create(long maxmsg = 2, long msgsize = 8) {
    static unsigned sequence = 0;
    struct mq_attr attr = {};
    // These members are ignored at creation; oflag controls nonblocking mode.
    attr.mq_flags = -1;
    attr.mq_curmsgs = -1;
    attr.mq_maxmsg = maxmsg;
    attr.mq_msgsize = msgsize;
    int saved_errno = libc_errno;
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
      LIBC_NAMESPACE::snprintf(name, sizeof(name), "/llvm-libc-mqueue-%ld-%u",
                               static_cast<long>(LIBC_NAMESPACE::getpid()),
                               sequence++);
      libc_errno = saved_errno;
      mqdes =
          LIBC_NAMESPACE::mq_open(name, O_CREAT | O_EXCL | O_RDWR | O_NONBLOCK,
                                  static_cast<mode_t>(0600), &attr);
      if (mqdes >= 0) {
        linked = true;
        return mqdes;
      }
      if (libc_errno != EEXIST)
        break;
    }
    return mqdes;
  }

  void TearDown() override {
    // Keep ownership in the fixture so an assertion cannot leak a queue or
    // descriptor. Never remove a pre-existing queue after an EEXIST collision.
    if (linked)
      EXPECT_THAT(LIBC_NAMESPACE::mq_unlink(name), Succeeds());
    if (duplicate >= 0)
      EXPECT_THAT(LIBC_NAMESPACE::mq_close(duplicate), Succeeds());
    if (other >= 0)
      EXPECT_THAT(LIBC_NAMESPACE::mq_close(other), Succeeds());
    if (mqdes >= 0)
      EXPECT_THAT(LIBC_NAMESPACE::mq_close(mqdes), Succeeds());
    LIBC_NAMESPACE::testing::ErrnoCheckingTest::TearDown();
  }
};

} // namespace

TEST_F(LlvmLibcMqueueTest, CreateReopenAndUnlinkLifetime) {
  libc_errno = EDOM;
  ASSERT_GE(create(), 0);
  ASSERT_ERRNO_EQ(EDOM);

  struct mq_attr attr = {};
  libc_errno = EDOM;
  ASSERT_THAT(LIBC_NAMESPACE::mq_getattr(mqdes, &attr), Succeeds(0, EDOM));
  EXPECT_EQ(attr.mq_flags, static_cast<long>(O_NONBLOCK));
  EXPECT_EQ(attr.mq_maxmsg, 2L);
  EXPECT_EQ(attr.mq_msgsize, 8L);
  EXPECT_EQ(attr.mq_curmsgs, 0L);
  // Linux creates the descriptor close-on-exec even without O_CLOEXEC.
  EXPECT_THAT(LIBC_NAMESPACE::fcntl(mqdes, F_GETFD), Succeeds(FD_CLOEXEC));

  other = LIBC_NAMESPACE::mq_open(name, O_CREAT | O_EXCL | O_RDWR | O_NONBLOCK,
                                  static_cast<mode_t>(0600), &attr);
  ASSERT_THAT(other, Fails(EEXIST));
  // No optional arguments may be read without O_CREAT, including with O_EXCL.
  other = LIBC_NAMESPACE::mq_open(name, O_RDWR | O_NONBLOCK | O_EXCL);
  ASSERT_GE(other, 0);
  ASSERT_ERRNO_SUCCESS();
  EXPECT_THAT(LIBC_NAMESPACE::fcntl(other, F_GETFD), Succeeds(FD_CLOEXEC));

  duplicate = LIBC_NAMESPACE::mq_open(name, O_CREAT | O_RDWR | O_NONBLOCK,
                                      static_cast<mode_t>(0600),
                                      static_cast<struct mq_attr *>(nullptr));
  ASSERT_GE(duplicate, 0);
  ASSERT_ERRNO_SUCCESS();
  ASSERT_THAT(LIBC_NAMESPACE::mq_getattr(duplicate, &attr), Succeeds());
  EXPECT_EQ(attr.mq_maxmsg, 2L);
  EXPECT_EQ(attr.mq_msgsize, 8L);
  ASSERT_THAT(LIBC_NAMESPACE::mq_close(duplicate), Succeeds());
  duplicate = -1;

  libc_errno = EDOM;
  ASSERT_THAT(LIBC_NAMESPACE::mq_unlink(name), Succeeds(0, EDOM));
  linked = false;
  EXPECT_THAT(LIBC_NAMESPACE::mq_unlink(name), Fails(ENOENT));
  duplicate = LIBC_NAMESPACE::mq_open(name, O_RDWR | O_NONBLOCK);
  ASSERT_THAT(duplicate, Fails(ENOENT));

  ASSERT_THAT(LIBC_NAMESPACE::mq_send(mqdes, "x", 1, 3), Succeeds());
  char buffer[8] = {};
  unsigned priority = 0;
  ASSERT_THAT(
      LIBC_NAMESPACE::mq_receive(other, buffer, sizeof(buffer), &priority),
      Succeeds<ssize_t>(1));
  EXPECT_EQ(buffer[0], 'x');
  EXPECT_EQ(priority, 3U);

  mqd_t closed = other;
  libc_errno = EDOM;
  ASSERT_THAT(LIBC_NAMESPACE::mq_close(other), Succeeds(0, EDOM));
  other = -1;
  EXPECT_THAT(LIBC_NAMESPACE::mq_getattr(closed, &attr), Fails(EBADF));
  EXPECT_THAT(LIBC_NAMESPACE::mq_close(closed), Fails(EBADF));
}

TEST_F(LlvmLibcMqueueTest, AttributesBelongToOpenDescription) {
  ASSERT_GE(create(), 0);
  ASSERT_ERRNO_SUCCESS();
  duplicate = LIBC_NAMESPACE::dup(mqdes);
  ASSERT_GE(duplicate, 0);
  ASSERT_ERRNO_SUCCESS();
  other = LIBC_NAMESPACE::mq_open(name, O_RDWR | O_NONBLOCK);
  ASSERT_GE(other, 0);
  ASSERT_ERRNO_SUCCESS();

  struct mq_attr input;
  input.mq_flags = 0;
  struct mq_attr old = {};
  libc_errno = EDOM;
  ASSERT_THAT(LIBC_NAMESPACE::mq_setattr(mqdes, &input, &old),
              Succeeds(0, EDOM));
  EXPECT_EQ(old.mq_flags, static_cast<long>(O_NONBLOCK));
  EXPECT_EQ(old.mq_maxmsg, 2L);
  EXPECT_EQ(old.mq_msgsize, 8L);
  EXPECT_EQ(old.mq_curmsgs, 0L);

  struct mq_attr current = {};
  ASSERT_THAT(LIBC_NAMESPACE::mq_getattr(duplicate, &current), Succeeds());
  EXPECT_EQ(current.mq_flags, 0L);
  ASSERT_THAT(LIBC_NAMESPACE::mq_getattr(other, &current), Succeeds());
  EXPECT_EQ(current.mq_flags, static_cast<long>(O_NONBLOCK));

  input.mq_flags = O_NONBLOCK;
  ASSERT_THAT(LIBC_NAMESPACE::mq_setattr(duplicate, &input, nullptr),
              Succeeds());
  ASSERT_THAT(LIBC_NAMESPACE::mq_getattr(mqdes, &current), Succeeds());
  EXPECT_EQ(current.mq_flags, static_cast<long>(O_NONBLOCK));
  EXPECT_EQ(current.mq_maxmsg, 2L);
  EXPECT_EQ(current.mq_msgsize, 8L);

  input.mq_flags = -1;
  old.mq_flags = 123;
  old.mq_maxmsg = 456;
  old.mq_msgsize = 789;
  old.mq_curmsgs = 42;
  EXPECT_THAT(LIBC_NAMESPACE::mq_setattr(mqdes, &input, &old), Fails(EINVAL));
  EXPECT_EQ(old.mq_flags, 123L);
  EXPECT_EQ(old.mq_maxmsg, 456L);
  EXPECT_EQ(old.mq_msgsize, 789L);
  EXPECT_EQ(old.mq_curmsgs, 42L);
  ASSERT_THAT(LIBC_NAMESPACE::mq_getattr(mqdes, &current), Succeeds());
  EXPECT_EQ(current.mq_flags, static_cast<long>(O_NONBLOCK));
}

TEST_F(LlvmLibcMqueueTest, PriorityFifoCapacityAndZeroLength) {
  ASSERT_GE(create(3), 0);
  ASSERT_ERRNO_SUCCESS();
  const char low[] = {'a', '\0', 'c', 'd'};
  libc_errno = EDOM;
  ASSERT_THAT(LIBC_NAMESPACE::mq_send(mqdes, low, sizeof(low), 1),
              Succeeds(0, EDOM));
  ASSERT_THAT(LIBC_NAMESPACE::mq_send(mqdes, "b", 1, 7), Succeeds());
  ASSERT_THAT(LIBC_NAMESPACE::mq_send(mqdes, "c", 1, 7), Succeeds());
  EXPECT_THAT(LIBC_NAMESPACE::mq_send(mqdes, "d", 1, 0), Fails(EAGAIN));

  struct mq_attr attr = {};
  ASSERT_THAT(LIBC_NAMESPACE::mq_getattr(mqdes, &attr), Succeeds());
  EXPECT_EQ(attr.mq_curmsgs, 3L);
  char buffer[8] = {};
  unsigned priority = 99;
  const char expected_messages[] = {'b', 'c'};
  for (char expected : expected_messages) {
    libc_errno = EDOM;
    ASSERT_THAT(
        LIBC_NAMESPACE::mq_receive(mqdes, buffer, sizeof(buffer), &priority),
        Succeeds<ssize_t>(1, EDOM));
    EXPECT_EQ(buffer[0], expected);
    EXPECT_EQ(priority, 7U);
  }
  buffer[4] = 'z';
  ASSERT_THAT(
      LIBC_NAMESPACE::mq_receive(mqdes, buffer, sizeof(buffer), &priority),
      Succeeds<ssize_t>(sizeof(low)));
  EXPECT_EQ(priority, 1U);
  for (size_t i = 0; i < sizeof(low); ++i)
    EXPECT_EQ(buffer[i], low[i]);
  EXPECT_EQ(buffer[4], 'z');

  buffer[0] = 'z';
  priority = 99;
  EXPECT_THAT(
      LIBC_NAMESPACE::mq_receive(mqdes, buffer, sizeof(buffer), &priority),
      Fails<ssize_t>(EAGAIN));
  EXPECT_EQ(buffer[0], 'z');
  EXPECT_EQ(priority, 99U);
  ASSERT_THAT(LIBC_NAMESPACE::mq_send(mqdes, "", 0, 0), Succeeds());
  ASSERT_THAT(
      LIBC_NAMESPACE::mq_receive(mqdes, buffer, sizeof(buffer), nullptr),
      Succeeds<ssize_t>(0));
  EXPECT_EQ(buffer[0], 'z');
  ASSERT_THAT(LIBC_NAMESPACE::mq_getattr(mqdes, &attr), Succeeds());
  EXPECT_EQ(attr.mq_curmsgs, 0L);
}

TEST_F(LlvmLibcMqueueTest, NullAndWideTimeouts) {
  ASSERT_GE(create(1), 0);
  ASSERT_ERRNO_SUCCESS();
  char buffer[8] = {};
  unsigned priority = 0;
  EXPECT_THAT(LIBC_NAMESPACE::mq_timedreceive(mqdes, buffer, sizeof(buffer),
                                              &priority, nullptr),
              Fails<ssize_t>(EAGAIN));
  libc_errno = EDOM;
  ASSERT_THAT(LIBC_NAMESPACE::mq_timedsend(mqdes, "n", 1, 2, nullptr),
              Succeeds(0, EDOM));
  EXPECT_THAT(LIBC_NAMESPACE::mq_timedsend(mqdes, "n", 1, 2, nullptr),
              Fails(EAGAIN));
  libc_errno = EDOM;
  ASSERT_THAT(LIBC_NAMESPACE::mq_timedreceive(mqdes, buffer, sizeof(buffer),
                                              &priority, nullptr),
              Succeeds<ssize_t>(1, EDOM));
  EXPECT_EQ(buffer[0], 'n');
  EXPECT_EQ(priority, 2U);

  const struct timespec wide = {static_cast<time_t>(1LL << 33), 123456789};
  ASSERT_THAT(LIBC_NAMESPACE::mq_timedsend(mqdes, "w", 1, 4, &wide),
              Succeeds());
  EXPECT_THAT(LIBC_NAMESPACE::mq_timedsend(mqdes, "w", 1, 4, &wide),
              Fails(EAGAIN));
  ASSERT_THAT(LIBC_NAMESPACE::mq_timedreceive(mqdes, buffer, sizeof(buffer),
                                              &priority, &wide),
              Succeeds<ssize_t>(1));
  EXPECT_EQ(buffer[0], 'w');
  EXPECT_EQ(priority, 4U);
  EXPECT_THAT(LIBC_NAMESPACE::mq_timedreceive(mqdes, buffer, sizeof(buffer),
                                              &priority, &wide),
              Fails<ssize_t>(EAGAIN));
  EXPECT_EQ(wide.tv_sec, static_cast<time_t>(1LL << 33));
  EXPECT_EQ(wide.tv_nsec, static_cast<decltype(wide.tv_nsec)>(123456789));
}

TEST_F(LlvmLibcMqueueTest, ExpiredAndInvalidTimeouts) {
  ASSERT_GE(create(1), 0);
  ASSERT_ERRNO_SUCCESS();
  char buffer[8] = {};
  const struct timespec expired = {0, 0};
  // O_NONBLOCK wins over an expired deadline when no transfer is possible.
  EXPECT_THAT(LIBC_NAMESPACE::mq_timedreceive(mqdes, buffer, sizeof(buffer),
                                              nullptr, &expired),
              Fails<ssize_t>(EAGAIN));
  const struct timespec invalid[] = {{-1, 0}, {0, -1}, {0, 1000000000}};
  for (const auto &timeout : invalid) {
    EXPECT_THAT(LIBC_NAMESPACE::mq_timedsend(mqdes, "x", 1, 0, &timeout),
                Fails(EINVAL));
    EXPECT_THAT(LIBC_NAMESPACE::mq_timedreceive(mqdes, buffer, sizeof(buffer),
                                                nullptr, &timeout),
                Fails<ssize_t>(EINVAL));
  }

  struct mq_attr attr = {};
  ASSERT_THAT(LIBC_NAMESPACE::mq_setattr(mqdes, &attr, nullptr), Succeeds());
  // The only blocking-mode operations use an already-expired absolute time.
  EXPECT_THAT(LIBC_NAMESPACE::mq_timedreceive(mqdes, buffer, sizeof(buffer),
                                              nullptr, &expired),
              Fails<ssize_t>(ETIMEDOUT));
  ASSERT_THAT(LIBC_NAMESPACE::mq_timedsend(mqdes, "x", 1, 0, &expired),
              Succeeds());
  EXPECT_THAT(LIBC_NAMESPACE::mq_timedsend(mqdes, "y", 1, 0, &expired),
              Fails(ETIMEDOUT));
  ASSERT_THAT(LIBC_NAMESPACE::mq_timedreceive(mqdes, buffer, sizeof(buffer),
                                              nullptr, &expired),
              Succeeds<ssize_t>(1));
  EXPECT_EQ(buffer[0], 'x');
}

TEST_F(LlvmLibcMqueueTest, AccessModesAndMessageErrors) {
  ASSERT_GE(create(1), 0);
  ASSERT_ERRNO_SUCCESS();
  other = LIBC_NAMESPACE::mq_open(name, O_RDONLY | O_NONBLOCK);
  ASSERT_GE(other, 0);
  ASSERT_ERRNO_SUCCESS();
  duplicate = LIBC_NAMESPACE::mq_open(name, O_WRONLY | O_NONBLOCK);
  ASSERT_GE(duplicate, 0);
  ASSERT_ERRNO_SUCCESS();
  char buffer[9] = {};
  EXPECT_THAT(LIBC_NAMESPACE::mq_send(other, "x", 1, 0), Fails(EBADF));
  EXPECT_THAT(
      LIBC_NAMESPACE::mq_receive(duplicate, buffer, sizeof(buffer), nullptr),
      Fails<ssize_t>(EBADF));
  EXPECT_THAT(LIBC_NAMESPACE::mq_send(mqdes, buffer, sizeof(buffer), 0),
              Fails(EMSGSIZE));
  EXPECT_THAT(LIBC_NAMESPACE::mq_send(mqdes, "x", 1, ~0U), Fails(EINVAL));
  EXPECT_THAT(LIBC_NAMESPACE::mq_send(mqdes, nullptr, 1, 0), Fails(EFAULT));

  ASSERT_THAT(LIBC_NAMESPACE::mq_send(duplicate, "x", 1, 0), Succeeds());
  // Receive capacity must cover mq_msgsize, even for a one-byte message.
  EXPECT_THAT(LIBC_NAMESPACE::mq_receive(other, buffer, 1, nullptr),
              Fails<ssize_t>(EMSGSIZE));
  ASSERT_THAT(
      LIBC_NAMESPACE::mq_receive(other, buffer, sizeof(buffer), nullptr),
      Succeeds<ssize_t>(1));
  EXPECT_EQ(buffer[0], 'x');
  ASSERT_THAT(LIBC_NAMESPACE::mq_send(mqdes, "x", 1, 0), Succeeds());
  EXPECT_THAT(LIBC_NAMESPACE::mq_receive(mqdes, nullptr, 8, nullptr),
              Fails<ssize_t>(EFAULT));
}

TEST_F(LlvmLibcMqueueTest, InvalidDescriptorsPreserveOutput) {
  char buffer[8] = {'z'};
  unsigned priority = 99;
  struct mq_attr attr = {};
  struct mq_attr output = {};
  output.mq_flags = 123;
  output.mq_maxmsg = 456;
  output.mq_msgsize = 789;
  output.mq_curmsgs = 42;
  EXPECT_THAT(LIBC_NAMESPACE::mq_close(-1), Fails(EBADF));
  EXPECT_THAT(LIBC_NAMESPACE::mq_getattr(-1, &output), Fails(EBADF));
  EXPECT_THAT(LIBC_NAMESPACE::mq_setattr(-1, &attr, &output), Fails(EBADF));
  EXPECT_EQ(output.mq_flags, 123L);
  EXPECT_EQ(output.mq_maxmsg, 456L);
  EXPECT_EQ(output.mq_msgsize, 789L);
  EXPECT_EQ(output.mq_curmsgs, 42L);
  EXPECT_THAT(LIBC_NAMESPACE::mq_send(-1, "x", 1, 0), Fails(EBADF));
  EXPECT_THAT(LIBC_NAMESPACE::mq_receive(-1, buffer, sizeof(buffer), &priority),
              Fails<ssize_t>(EBADF));
  const struct timespec expired = {0, 0};
  EXPECT_THAT(LIBC_NAMESPACE::mq_timedsend(-1, "x", 1, 0, &expired),
              Fails(EBADF));
  EXPECT_THAT(LIBC_NAMESPACE::mq_timedreceive(-1, buffer, sizeof(buffer),
                                              &priority, &expired),
              Fails<ssize_t>(EBADF));
  EXPECT_EQ(buffer[0], 'z');
  EXPECT_EQ(priority, 99U);
}

TEST_F(LlvmLibcMqueueTest, InvalidNames) {
  EXPECT_THAT(LIBC_NAMESPACE::mq_open("", O_RDWR), Fails(EINVAL));
  EXPECT_THAT(LIBC_NAMESPACE::mq_open("llvm-libc-mqueue", O_RDWR),
              Fails(EINVAL));
  EXPECT_THAT(LIBC_NAMESPACE::mq_unlink(""), Fails(EINVAL));
  EXPECT_THAT(LIBC_NAMESPACE::mq_unlink("llvm-libc-mqueue"), Fails(EINVAL));
  EXPECT_THAT(LIBC_NAMESPACE::mq_open("/", O_RDWR), Fails(ENOENT));
  EXPECT_THAT(LIBC_NAMESPACE::mq_unlink("/"), Fails(ENOENT));
  EXPECT_THAT(LIBC_NAMESPACE::mq_open("//llvm-libc-mqueue", O_RDWR),
              Fails(EACCES));
  EXPECT_THAT(LIBC_NAMESPACE::mq_unlink("//llvm-libc-mqueue"), Fails(EACCES));
  EXPECT_THAT(LIBC_NAMESPACE::mq_open("/dev/mqueue/llvm-libc-mqueue", O_RDWR),
              Fails(EACCES));
  EXPECT_THAT(LIBC_NAMESPACE::mq_unlink("/dev/mqueue/llvm-libc-mqueue"),
              Fails(EACCES));

  char long_name[258];
  long_name[0] = '/';
  for (size_t i = 1; i < sizeof(long_name) - 1; ++i)
    long_name[i] = 'x';
  long_name[sizeof(long_name) - 1] = '\0';
  EXPECT_THAT(LIBC_NAMESPACE::mq_open(long_name, O_RDWR), Fails(ENAMETOOLONG));
  EXPECT_THAT(LIBC_NAMESPACE::mq_unlink(long_name), Fails(ENAMETOOLONG));
}

TEST_F(LlvmLibcMqueueTest, InvalidCreationAttributes) {
  ASSERT_THAT(create(0), Fails(EINVAL));
  ASSERT_THAT(create(1, 0), Fails(EINVAL));
  ASSERT_THAT(create(-1), Fails(EINVAL));
  ASSERT_THAT(create(1, -1), Fails(EINVAL));
}
