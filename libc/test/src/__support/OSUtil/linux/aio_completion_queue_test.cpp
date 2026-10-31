//===-- Tests for private Linux AIO completion routing
//---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/linux/aio_completion_queue.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;

namespace {
struct FakeRing {
  int poll_error = EAGAIN;
  int integrity_error = 0;
  int integrity_after_poll = 0;
  int enqueue_error = 0;
  int withdrawal_error = 0;
  unsigned polls = 0;
  unsigned checks = 0;
  uint64_t enqueued_token = 0;
  io_uring_cqe completion{};

  ErrorOr<void> enqueue(const io_uring_sqe &sqe) {
    if (enqueue_error)
      return Error(enqueue_error);
    enqueued_token = sqe.user_data;
    return {};
  }
  ErrorOr<void> poll(io_uring_cqe &cqe) {
    ++polls;
    integrity_error = integrity_after_poll;
    if (poll_error)
      return Error(poll_error);
    cqe = completion;
    poll_error = EAGAIN;
    return {};
  }
  ErrorOr<void> withdraw_last(uint64_t token) {
    if (withdrawal_error)
      return Error(withdrawal_error);
    if (!enqueued_token)
      return Error(EAGAIN);
    if (enqueued_token != token)
      return Error(EINVAL);
    enqueued_token = 0;
    return {};
  }
  ErrorOr<void> check_integrity() {
    ++checks;
    if (integrity_error)
      return Error(integrity_error);
    return {};
  }
  void complete(uint64_t token, int32_t result = 0, uint32_t flags = 0) {
    completion = {};
    completion.user_data = token;
    completion.res = result;
    completion.flags = flags;
    poll_error = 0;
  }
};

using Queue = LIBC_NAMESPACE::AioCompletionQueue<FakeRing>;
using Kind = Queue::Kind;

// No destructors on the unexpected-success path: a later outstanding-entry
// failure must not make a broken validation test pass.
#if LIBC_TEST_SUBPROCESS_TESTS
[[noreturn]] void unexpected_success() {
  (void)LIBC_NAMESPACE::syscall_impl<long>(SYS_exit_group, 0);
  __builtin_trap();
}
#endif
} // namespace

TEST(LlvmLibcAioCompletionQueueTest, InitializationAndBounds) {
  FakeRing ring;
  uint64_t watermark = 0;
  Queue queue(ring, watermark);
  Queue::Entry entry;
  EXPECT_EQ(queue.prepare(entry, Kind::Transfer, nullptr, 1).error(), EINVAL);
  ring.integrity_error = EIO;
  EXPECT_EQ(queue.initialize(2).error(), EIO);
  ring.integrity_error = 0;
  EXPECT_EQ(queue.initialize(0).error(), EINVAL);
  EXPECT_EQ(queue.initialize((uint32_t(1) << 30) + 1).error(), EINVAL);
  ASSERT_TRUE(queue.initialize(2).has_value());
  EXPECT_EQ(queue.initialize(2).error(), EBUSY);
  EXPECT_EQ(queue.prepare(entry, Kind::Transfer, nullptr, UINT32_MAX).error(),
            EINVAL);
  const Kind zero_bound_kinds[] = {Kind::Metadata, Kind::Sync, Kind::Cancel};
  for (Kind kind : zero_bound_kinds)
    EXPECT_EQ(queue.prepare(entry, kind, nullptr, 1).error(), EINVAL);
  EXPECT_EQ(watermark, uint64_t(0));
  EXPECT_TRUE(entry.reclaimable());
  ASSERT_TRUE(
      queue.prepare(entry, Kind::Transfer, nullptr, INT32_MAX).has_value());
  EXPECT_TRUE(queue.rollback(entry));
}

TEST(LlvmLibcAioCompletionQueueTest, AdmissionRollbackBurnsAcrossRestarts) {
  FakeRing ring;
  uint64_t watermark = 0;
  Queue::Entry entry;
  {
    Queue queue(ring, watermark);
    ASSERT_TRUE(queue.initialize(2).has_value());
    auto token = queue.prepare(entry, Kind::Transfer, &entry, 16);
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(*token, uint64_t(1));
    EXPECT_EQ(queue.prepare(entry, Kind::Transfer, nullptr, 0).error(), EINVAL);
    io_uring_sqe sqe{};
    sqe.user_data = *token;
    ring.enqueue_error = EAGAIN;
    EXPECT_EQ(ring.enqueue(sqe).error(), EAGAIN);
    EXPECT_TRUE(queue.rollback(entry));
    EXPECT_FALSE(queue.rollback(entry));
    EXPECT_TRUE(entry.reclaimable());
    EXPECT_EQ(queue.available(), 2U);
  }
  Queue restarted(ring, watermark);
  ASSERT_TRUE(restarted.initialize(2).has_value());
  auto token = restarted.prepare(entry, Kind::Transfer, &entry, 16);
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(*token, uint64_t(2));
  io_uring_sqe sqe{};
  sqe.user_data = *token;
  ring.enqueue_error = 0;
  ASSERT_TRUE(ring.enqueue(sqe).has_value());
  restarted.publish(entry);
  EXPECT_FALSE(restarted.rollback(entry));
  ring.complete(ring.enqueued_token, 7);
  auto completion = restarted.poll();
  ASSERT_TRUE(completion.has_value());
  EXPECT_EQ(completion->token, *token);
  EXPECT_EQ(completion->context, static_cast<void *>(&entry));
  EXPECT_TRUE(completion->kind == Kind::Transfer);
  EXPECT_EQ(completion->raw_result, 7);
  EXPECT_TRUE(entry.reclaimable());
  EXPECT_EQ(entry.token(), uint64_t(0));
}

TEST(LlvmLibcAioCompletionQueueTest, FullBudgetAndIndependentCancel) {
  const bool orders[] = {false, true};
  for (bool cancel_first : orders) {
    FakeRing ring;
    uint64_t watermark = 0;
    Queue queue(ring, watermark);
    Queue::Entry original, cancel, extra;
    ASSERT_TRUE(queue.initialize(2).has_value());
    auto original_token = queue.prepare(original, Kind::Transfer, &original, 8);
    ASSERT_TRUE(original_token.has_value());
    queue.publish(original);
    auto cancel_token = queue.prepare(cancel, Kind::Cancel, &cancel, 0);
    ASSERT_TRUE(cancel_token.has_value());
    queue.publish(cancel);
    EXPECT_EQ(queue.available(), 0U);
    EXPECT_EQ(queue.prepare(extra, Kind::Cancel, nullptr, 0).error(), EAGAIN);
    EXPECT_EQ(queue.prepare(extra, Kind::Transfer, nullptr, 1).error(), EAGAIN);
    EXPECT_EQ(watermark, uint64_t(2));
    ring.complete(cancel_first ? *cancel_token : *original_token,
                  cancel_first ? 0 : -ECANCELED);
    auto first = queue.poll();
    ASSERT_TRUE(first.has_value());
    EXPECT_TRUE(first->kind == (cancel_first ? Kind::Cancel : Kind::Transfer));
    EXPECT_EQ(first->context, cancel_first ? static_cast<void *>(&cancel)
                                           : static_cast<void *>(&original));
    EXPECT_EQ(queue.outstanding(), 1U);
    EXPECT_EQ(original.reclaimable(), !cancel_first);
    EXPECT_EQ(cancel.reclaimable(), cancel_first);
    ASSERT_TRUE(queue.prepare(extra, Kind::Sync, nullptr, 0).has_value());
    EXPECT_TRUE(queue.rollback(extra));
    ring.complete(cancel_first ? *original_token : *cancel_token,
                  cancel_first ? -ECANCELED : -ENOENT);
    auto second = queue.poll();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->raw_result, cancel_first ? -ECANCELED : -ENOENT);
    EXPECT_TRUE(original.reclaimable());
    EXPECT_TRUE(cancel.reclaimable());
    EXPECT_EQ(queue.available(), 2U);
  }
}

TEST(LlvmLibcAioCompletionQueueTest, WithdrawnTailRetiresOnlyItsOwnCredit) {
  FakeRing ring;
  uint64_t watermark = 0;
  Queue queue(ring, watermark);
  Queue::Entry consumed, tail, fresh;
  ASSERT_TRUE(queue.initialize(3).has_value());
  auto first = queue.prepare(consumed, Kind::Transfer, &consumed, 1);
  ASSERT_TRUE(first.has_value());
  queue.publish(consumed);
  auto second = queue.prepare(tail, Kind::Metadata, &tail, 0);
  ASSERT_TRUE(second.has_value());
  io_uring_sqe sqe{};
  sqe.user_data = *second;
  ASSERT_TRUE(ring.enqueue(sqe).has_value());
  queue.publish(tail);
  EXPECT_EQ(queue.withdraw(fresh).error(), EINVAL);
  EXPECT_EQ(queue.withdraw(consumed).error(), EINVAL);
  EXPECT_EQ(queue.outstanding(), 2U);
  ASSERT_TRUE(queue.withdraw(tail).has_value());
  EXPECT_TRUE(tail.reclaimable());
  EXPECT_FALSE(consumed.reclaimable());
  EXPECT_EQ(queue.outstanding(), 1U);
  EXPECT_EQ(queue.withdraw(consumed).error(), EAGAIN);
  EXPECT_EQ(queue.withdraw(tail).error(), EINVAL);
  EXPECT_EQ(ring.polls, 0U);
  auto third = queue.prepare(tail, Kind::Cancel, nullptr, 0);
  ASSERT_TRUE(third.has_value());
  EXPECT_GT(*third, *second);
  EXPECT_TRUE(queue.rollback(tail));
  ring.complete(*first, 1);
  ASSERT_TRUE(queue.poll().has_value());
  EXPECT_EQ(queue.outstanding(), 0U);
}

TEST(LlvmLibcAioCompletionQueueTest, WithdrawalRefusalPreservesEntry) {
  FakeRing ring;
  uint64_t watermark = 0;
  Queue queue(ring, watermark);
  Queue::Entry entry;
  ASSERT_TRUE(queue.initialize(1).has_value());
  auto token = queue.prepare(entry, Kind::Sync, &entry, 0);
  ASSERT_TRUE(token.has_value());
  queue.publish(entry);
  const int errors[] = {EAGAIN, EINVAL};
  for (int error : errors) {
    ring.withdrawal_error = error;
    EXPECT_EQ(queue.withdraw(entry).error(), error);
    EXPECT_FALSE(entry.reclaimable());
    EXPECT_EQ(entry.token(), *token);
    EXPECT_EQ(queue.available(), 0U);
  }
  ring.complete(*token);
  EXPECT_TRUE(queue.poll().has_value());
}

TEST(LlvmLibcAioCompletionQueueTest, TransportErrorsPreserveOwnership) {
  FakeRing ring;
  uint64_t watermark = 0;
  Queue queue(ring, watermark);
  Queue::Entry entry, unpublished;
  ASSERT_TRUE(queue.initialize(2).has_value());
  auto token = queue.prepare(entry, Kind::Metadata, &entry, 0);
  ASSERT_TRUE(token.has_value());
  queue.publish(entry);
  auto pending = queue.prepare(unpublished, Kind::Sync, nullptr, 0);
  ASSERT_TRUE(pending.has_value());
  const int errors[] = {EAGAIN, EINTR, ENOMEM, EIO, EINVAL, EPROTO};
  for (int error : errors) {
    ring.poll_error = error;
    unsigned checks = ring.checks;
    EXPECT_EQ(queue.poll().error(), error);
    EXPECT_EQ(ring.checks, checks + 1);
    EXPECT_EQ(queue.outstanding(), 2U);
    EXPECT_EQ(entry.token(), *token);
    EXPECT_EQ(unpublished.token(), *pending);
    EXPECT_FALSE(entry.reclaimable());
  }
  EXPECT_TRUE(queue.rollback(unpublished));
  ring.complete(*token, -4095);
  auto completion = queue.poll();
  ASSERT_TRUE(completion.has_value());
  EXPECT_EQ(completion->raw_result, -4095);
  EXPECT_TRUE(completion->kind == Kind::Metadata);
  EXPECT_EQ(queue.outstanding(), 0U);
}

TEST(LlvmLibcAioCompletionQueueTest, ExhaustedTokensNeverWrap) {
  FakeRing ring;
  uint64_t watermark = UINT64_MAX - 1;
  Queue queue(ring, watermark);
  Queue::Entry entry;
  ASSERT_TRUE(queue.initialize(1).has_value());
  ASSERT_TRUE(queue.prepare(entry, Kind::Sync, nullptr, 0).has_value());
  EXPECT_EQ(entry.token(), UINT64_MAX);
  EXPECT_TRUE(queue.rollback(entry));
  EXPECT_EQ(queue.prepare(entry, Kind::Sync, nullptr, 0).error(), EOVERFLOW);
  EXPECT_TRUE(entry.reclaimable());
  EXPECT_EQ(queue.available(), 1U);
}

TEST(LlvmLibcAioCompletionQueueTest, TerminalResultBoundaries) {
  FakeRing ring;
  uint64_t watermark = 0;
  Queue queue(ring, watermark);
  Queue::Entry entry;
  ASSERT_TRUE(queue.initialize(1).has_value());
  const Kind kinds[] = {Kind::Transfer, Kind::Metadata, Kind::Sync,
                        Kind::Cancel};
  for (Kind kind : kinds) {
    const int32_t results[] = {0, -1, -4095,
                               kind == Kind::Transfer ? INT32_MAX : 0};
    for (int32_t result : results) {
      auto token = queue.prepare(entry, kind, &entry,
                                 kind == Kind::Transfer ? INT32_MAX : 0);
      ASSERT_TRUE(token.has_value());
      queue.publish(entry);
      ring.complete(*token, result);
      auto completion = queue.poll();
      ASSERT_TRUE(completion.has_value());
      EXPECT_TRUE(completion->kind == kind);
      EXPECT_EQ(completion->token, *token);
      EXPECT_EQ(completion->context, static_cast<void *>(&entry));
      EXPECT_EQ(completion->raw_result, result);
      EXPECT_TRUE(entry.reclaimable());
      EXPECT_EQ(queue.available(), 1U);
    }
  }
}

TEST(LlvmLibcAioCompletionQueueTest, ForeignEntryCannotBeReclaimed) {
  FakeRing ring, other_ring;
  uint64_t watermark = 0, other_watermark = 0;
  Queue queue(ring, watermark), other(other_ring, other_watermark);
  Queue::Entry entry;
  ASSERT_TRUE(queue.initialize(1).has_value());
  ASSERT_TRUE(other.initialize(1).has_value());
  ASSERT_TRUE(queue.prepare(entry, Kind::Sync, nullptr, 0).has_value());
  EXPECT_FALSE(other.rollback(entry));
  EXPECT_EQ(other.prepare(entry, Kind::Sync, nullptr, 0).error(), EINVAL);
  EXPECT_EQ(queue.outstanding(), 1U);
  EXPECT_EQ(other.outstanding(), 0U);
  EXPECT_EQ(other_watermark, uint64_t(0));
  EXPECT_TRUE(queue.rollback(entry));
}

#if LIBC_TEST_SUBPROCESS_TESTS
TEST(LlvmLibcAioCompletionQueueTest, InvalidTokensExit127) {
  // Zero, unknown, unpublished, rolled back, duplicate, wrong ring and local
  // result tokens must never route to a kernel operation.
  for (unsigned scenario = 0; scenario != 7; ++scenario) {
    EXPECT_EXITS(
        ([scenario] {
          FakeRing ring, other_ring;
          uint64_t watermark = 0, other_watermark = 100;
          Queue queue(ring, watermark), other(other_ring, other_watermark);
          Queue::Entry entry, foreign;
          (void)queue.initialize(2);
          (void)other.initialize(2);
          uint64_t token = *queue.prepare(entry, Kind::Transfer, nullptr, 4);
          if (scenario != 2 && scenario != 3)
            queue.publish(entry);
          if (scenario == 0)
            token = 0;
          if (scenario == 1)
            token = 99;
          if (scenario == 3) {
            (void)queue.rollback(entry);
            (void)queue.prepare(entry, Kind::Transfer, nullptr, 4);
            queue.publish(entry);
          }
          if (scenario == 4) {
            ring.complete(token);
            (void)queue.poll();
          }
          if (scenario == 5)
            token = *other.prepare(foreign, Kind::Transfer, nullptr, 4);
          if (scenario == 6)
            token = *queue.reserve_local_token();
          ring.complete(token);
          (void)queue.poll();
          unexpected_success();
        }),
        127);
  }
}

TEST(LlvmLibcAioCompletionQueueTest, WithdrawalIntegrityAndStaleCqeExit127) {
  for (unsigned scenario = 0; scenario != 3; ++scenario) {
    EXPECT_EXITS(([scenario] {
                   FakeRing ring;
                   uint64_t watermark = 0;
                   Queue queue(ring, watermark);
                   Queue::Entry entry;
                   (void)queue.initialize(1);
                   auto token = queue.prepare(entry, Kind::Sync, nullptr, 0);
                   ring.enqueued_token = *token;
                   queue.publish(entry);
                   if (scenario == 0)
                     ring.integrity_error = EOVERFLOW;
                   if (scenario == 1)
                     ring.withdrawal_error = EPROTO;
                   (void)queue.withdraw(entry);
                   if (scenario == 2) {
                     ring.complete(*token);
                     (void)queue.poll();
                   }
                   unexpected_success();
                 }),
                 127);
  }
}

TEST(LlvmLibcAioCompletionQueueTest, MalformedCompletionsExit127) {
  struct Case {
    Kind kind;
    int32_t result;
    uint32_t flags;
  };
  const Case cases[] = {
      {Kind::Transfer, 5, 0},         {Kind::Transfer, -4096, 0},
      {Kind::Transfer, INT32_MIN, 0}, {Kind::Transfer, -EINPROGRESS, 0},
      {Kind::Transfer, 0, 1},         {Kind::Transfer, 0, UINT32_MAX},
      {Kind::Metadata, 1, 0},         {Kind::Sync, 1, 0},
      {Kind::Cancel, 1, 0},
  };
  for (Case test : cases) {
    EXPECT_EXITS(([test] {
                   FakeRing ring;
                   uint64_t watermark = 0;
                   Queue queue(ring, watermark);
                   Queue::Entry entry;
                   (void)queue.initialize(1);
                   auto token =
                       queue.prepare(entry, test.kind, nullptr,
                                     test.kind == Kind::Transfer ? 4 : 0);
                   queue.publish(entry);
                   ring.complete(*token, test.result, test.flags);
                   (void)queue.poll();
                   unexpected_success();
                 }),
                 127);
  }
}

TEST(LlvmLibcAioCompletionQueueTest, IntegrityErrorsExit127RegardlessOfPoll) {
  const int health_errors[] = {EIO, EOVERFLOW, EPROTO, EINVAL};
  const int poll_errors[] = {0, EIO, EAGAIN, EINTR};
  for (int health_error : health_errors) {
    for (int poll_error : poll_errors) {
      EXPECT_EXITS(([=] {
                     FakeRing ring;
                     uint64_t watermark = 0;
                     Queue queue(ring, watermark);
                     Queue::Entry entry;
                     (void)queue.initialize(1);
                     auto token =
                         queue.prepare(entry, Kind::Transfer, nullptr, 1);
                     queue.publish(entry);
                     ring.complete(*token);
                     ring.poll_error = poll_error;
                     ring.integrity_after_poll = health_error;
                     (void)queue.poll();
                     unexpected_success();
                   }),
                   127);
    }
  }
}

TEST(LlvmLibcAioCompletionQueueTest, OutstandingDestructorExits127) {
  const bool states[] = {false, true};
  for (bool published : states) {
    EXPECT_EXITS(([published] {
                   FakeRing ring;
                   uint64_t watermark = 0;
                   Queue::Entry entry;
                   {
                     Queue queue(ring, watermark);
                     (void)queue.initialize(1);
                     (void)queue.prepare(entry, Kind::Sync, nullptr, 0);
                     if (published)
                       queue.publish(entry);
                   }
                   unexpected_success();
                 }),
                 127);
  }
}
#endif // LIBC_TEST_SUBPROCESS_TESTS
