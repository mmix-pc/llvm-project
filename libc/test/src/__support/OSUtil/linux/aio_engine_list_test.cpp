//===-- Tests for local AIO list-member results
//----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/linux/aio_engine.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;

namespace {
struct Ring {
  unsigned calls = 0, personalities = 0;
  uint64_t published_token = 0;
  bool accept_enqueue = false, completion_ready = false;
  ErrorOr<void> check_integrity() { return {}; }
  ErrorOr<unsigned> register_personality() { return ++personalities; }
  ErrorOr<void> unregister_personality(unsigned) {
    --personalities;
    return {};
  }
  ErrorOr<void> enqueue(const io_uring_sqe &sqe) {
    ++calls;
    if (!accept_enqueue)
      return Error(EIO);
    published_token = sqe.user_data;
    return {};
  }
  ErrorOr<unsigned> submit() {
    ++calls;
    return 0U;
  }
  ErrorOr<void> poll(io_uring_cqe &cqe) {
    if (!completion_ready)
      return Error(EAGAIN);
    completion_ready = false;
    cqe = {};
    cqe.user_data = published_token;
    return {};
  }
  ErrorOr<void> withdraw_last(uint64_t) { return Error(EAGAIN); }
};
struct Resources {
  ErrorOr<int> duplicate(int fd) { return fd + 100; }
  ErrorOr<int> close(int) { return 0; }
};
using Engine = LIBC_NAMESPACE::AioEngine<Ring, Resources>;
Engine::Input input(const void *key, int fd = -1) {
  return {key, fd, Engine::Operation::Read, nullptr, 0, 0, false};
}
} // namespace

TEST(LlvmLibcAioEngineListTest, RejectedMembersDoNotConsumeCompletionCredits) {
  Ring ring;
  uint64_t watermark = 0;
  Engine engine(ring, watermark, 1, 1);
  ASSERT_TRUE(bool(engine.initialize(2)));
  Engine::Request pending(ring), one(ring), two(ring), three(ring),
      excess(ring);
  int cb[5]{};
  ASSERT_TRUE(bool(engine.prepare_rejection(pending)));
  ASSERT_TRUE(bool(engine.prepare_rejection(one)));
  ASSERT_TRUE(bool(engine.prepare_rejection(two)));
  ASSERT_TRUE(bool(engine.prepare_rejection(three)));
  ASSERT_TRUE(bool(pending.resources.capture(7)));
  ASSERT_TRUE(bool(excess.resources.capture(8)));
  ASSERT_TRUE(bool(engine.admit(pending, input(&cb[0], 7))));
  EXPECT_EQ(pending.state().identity().token, uint64_t(5));
  EXPECT_EQ(engine.admit(excess, input(&cb[4], 8)).error(), EAGAIN);
  ASSERT_TRUE(bool(engine.prepare_rejection(excess)));
  EXPECT_EQ(engine.admit(excess, input(&cb[4], 8)).error(), EAGAIN);
  auto rejected = input(&cb[1]);
  rejected.notify = true;
  ASSERT_TRUE(bool(engine.admit_rejected(one, rejected, EBADF)));
  ASSERT_TRUE(bool(engine.admit_rejected(two, input(&cb[2]), EAGAIN)));
  ASSERT_TRUE(bool(engine.admit_rejected(three, input(&cb[3]), EINVAL)));
  EXPECT_EQ(watermark, uint64_t(6));
  EXPECT_EQ(one.state().identity().token, uint64_t(2));
  EXPECT_GT(three.state().identity().token, two.state().identity().token);
  EXPECT_EQ(ring.calls, 0U);
  EXPECT_EQ(ring.personalities, 2U);
  EXPECT_FALSE(engine.cancel_queued(one));
  EXPECT_FALSE(one.published());
  EXPECT_FALSE(bool(one.state().take_notification()));
  EXPECT_FALSE(engine.retire(one));
  Engine::Request *failed[] = {&one, &two, &three};
  const int errors[] = {EBADF, EAGAIN, EINVAL};
  for (unsigned i = 0; i != 3; ++i) {
    EXPECT_EQ(*engine.error(&cb[i + 1]), errors[i]);
    auto result = engine.take_result(&cb[i + 1]);
    ASSERT_TRUE(bool(result));
    EXPECT_EQ(result->value, ssize_t(-1));
    EXPECT_EQ(result->error, errors[i]);
    EXPECT_EQ(engine.take_result(&cb[i + 1]).error(), EINVAL);
    ASSERT_TRUE(engine.retire(*failed[i]));
  }
  EXPECT_EQ(*engine.error(&cb[0]), EINPROGRESS);
  ASSERT_TRUE(engine.cancel_queued(pending));
  ASSERT_TRUE(bool(engine.take_result(&cb[0])));
  ASSERT_TRUE(engine.retire(pending));
  ASSERT_TRUE(bool(engine.admit(excess, input(&cb[4], 8))));
  EXPECT_EQ(excess.state().identity().token, uint64_t(7));
  ASSERT_TRUE(engine.cancel_queued(excess));
  ASSERT_TRUE(bool(engine.take_result(&cb[4])));
  ASSERT_TRUE(engine.retire(excess));
  EXPECT_EQ(ring.personalities, 0U);
  EXPECT_TRUE(engine.empty());
}

TEST(LlvmLibcAioEngineListTest, ResultObserverSurvivesAddressReuse) {
  Ring ring;
  uint64_t watermark = 0;
  Engine engine(ring, watermark, 1, 1);
  ASSERT_TRUE(bool(engine.initialize(2)));
  Engine::Request old(ring), fresh(ring);
  LIBC_NAMESPACE::AioRequest::Reference observer;
  int cb = 0;
  ASSERT_TRUE(bool(engine.admit_rejected(old, input(&cb), EAGAIN)));
  ASSERT_TRUE(bool(old.state().retain(observer)));
  ASSERT_TRUE(bool(engine.take_result(&cb)));
  EXPECT_FALSE(engine.retire(old));
  ASSERT_TRUE(bool(engine.admit_rejected(fresh, input(&cb), EINVAL)));
  EXPECT_GT(fresh.state().identity().token, old.state().identity().token);
  auto result = old.state().result(observer);
  ASSERT_TRUE(bool(result));
  EXPECT_EQ(result->error, EAGAIN);
  EXPECT_EQ(*engine.error(&cb), EINVAL);
  ASSERT_TRUE(old.state().release(observer));
  ASSERT_TRUE(engine.retire(old));
  ASSERT_TRUE(bool(engine.take_result(&cb)));
  ASSERT_TRUE(engine.retire(fresh));
}

TEST(LlvmLibcAioEngineListTest, InvalidAndDuplicateBindingsDoNotBurnTokens) {
  Ring ring;
  uint64_t watermark = 0;
  Engine engine(ring, watermark, 1, 1);
  ASSERT_TRUE(bool(engine.initialize(2)));
  Engine::Request one(ring), two(ring);
  int cb = 0;
  EXPECT_EQ(engine.admit_rejected(one, input(nullptr), EIO).error(), EINVAL);
  EXPECT_EQ(engine.admit_rejected(one, input(&cb), 0).error(), EINVAL);
  EXPECT_EQ(engine.admit_rejected(one, input(&cb), EINPROGRESS).error(),
            EINVAL);
  EXPECT_EQ(watermark, uint64_t(0));
  ASSERT_TRUE(bool(engine.admit_rejected(one, input(&cb), EIO)));
  EXPECT_EQ(engine.admit_rejected(two, input(&cb), EBADF).error(), EBUSY);
  EXPECT_EQ(watermark, uint64_t(1));
  ASSERT_TRUE(bool(engine.take_result(&cb)));
  ASSERT_TRUE(engine.retire(one));
}

TEST(LlvmLibcAioEngineListTest, ExhaustionNeverPublishesOrReusesIdentity) {
  Ring ring;
  uint64_t watermark = UINT64_MAX - 1;
  Engine engine(ring, watermark, 1, 1);
  ASSERT_TRUE(bool(engine.initialize(2)));
  Engine::Request one(ring), two(ring);
  int cb[2]{};
  ASSERT_TRUE(bool(engine.admit_rejected(one, input(&cb[0]), EAGAIN)));
  EXPECT_EQ(watermark, UINT64_MAX);
  EXPECT_EQ(engine.admit_rejected(two, input(&cb[1]), EAGAIN).error(),
            EOVERFLOW);
  EXPECT_EQ(engine.find(&cb[1]), nullptr);
  ASSERT_TRUE(bool(engine.take_result(&cb[0])));
  ASSERT_TRUE(engine.retire(one));
  EXPECT_EQ(engine.admit_rejected(two, input(&cb[1]), EAGAIN).error(),
            EOVERFLOW);
  EXPECT_EQ(ring.calls, 0U);
}

TEST(LlvmLibcAioEngineListTest, PreparedResultSurvivesOriginalTokenExhaustion) {
  Ring ring;
  ring.accept_enqueue = true;
  uint64_t watermark = UINT64_MAX - 3;
  unsigned wakes = 0;
  auto wake = [](void *context, const void *) {
    ++*static_cast<unsigned *>(context);
  };
  Engine engine(ring, watermark, 1, 2, wake, &wakes);
  ASSERT_TRUE(bool(engine.initialize(3)));
  Engine::Request accepted(ring), rejected(ring), unprepared(ring);
  int cb[3]{};
  ASSERT_TRUE(bool(engine.prepare_rejection(accepted)));
  ASSERT_TRUE(bool(engine.prepare_rejection(rejected)));
  EXPECT_EQ(watermark, UINT64_MAX - 1);
  EXPECT_EQ(wakes, 0U);
  EXPECT_TRUE(engine.empty());
  ASSERT_TRUE(bool(accepted.resources.capture(7)));
  ASSERT_TRUE(bool(rejected.resources.capture(8)));
  ASSERT_TRUE(bool(engine.admit(accepted, input(&cb[0], 7))));
  EXPECT_EQ(accepted.state().identity().token, UINT64_MAX);
  EXPECT_EQ(engine.admit(rejected, input(&cb[1], 8)).error(), EOVERFLOW);
  EXPECT_EQ(engine.find(&cb[1]), nullptr);
  auto failure = input(&cb[1], 8);
  failure.notify = true;
  ASSERT_TRUE(bool(engine.admit_rejected(rejected, failure, EOVERFLOW)));
  EXPECT_EQ(rejected.state().identity().token, UINT64_MAX - 1);
  EXPECT_FALSE(bool(rejected.state().take_notification()));
  EXPECT_FALSE(rejected.published());
  EXPECT_EQ(wakes, 1U);
  EXPECT_EQ(ring.calls, 0U);
  EXPECT_EQ(engine.prepare_rejection(unprepared).error(), EOVERFLOW);
  EXPECT_EQ(engine.admit_rejected(unprepared, input(&cb[2]), EIO).error(),
            EOVERFLOW);
  EXPECT_EQ(*engine.error(&cb[0]), EINPROGRESS);
  EXPECT_EQ(*engine.error(&cb[1]), EOVERFLOW);
  EXPECT_FALSE(engine.retire(rejected));
  EXPECT_TRUE(rejected.resources.ready());
  EXPECT_EQ(ring.personalities, 2U);
  ASSERT_EQ(engine.progress(), 0);
  EXPECT_EQ(ring.published_token, UINT64_MAX);
  EXPECT_TRUE(accepted.published());
  EXPECT_FALSE(accepted.complete());
  EXPECT_EQ(wakes, 1U);
  ring.completion_ready = true;
  ASSERT_EQ(engine.progress(), 0);
  EXPECT_TRUE(accepted.complete());
  EXPECT_EQ(wakes, 2U);
  EXPECT_EQ(ring.calls, 3U);
  auto result = engine.take_result(&cb[1]);
  ASSERT_TRUE(bool(result));
  EXPECT_EQ(result->error, EOVERFLOW);
  ASSERT_TRUE(engine.retire(rejected));
  ASSERT_TRUE(bool(engine.take_result(&cb[0])));
  ASSERT_TRUE(engine.retire(accepted));
  EXPECT_EQ(ring.personalities, 0U);
  EXPECT_EQ(watermark, UINT64_MAX);
  EXPECT_TRUE(engine.empty());
}

TEST(LlvmLibcAioEngineListTest, FailedAdmissionPreservesPreparedIdentity) {
  Ring ring;
  uint64_t watermark = UINT64_MAX - 3;
  Engine engine(ring, watermark, 1, 1);
  ASSERT_TRUE(bool(engine.initialize(3)));
  Engine::Request pending(ring), rejected(ring);
  int cb[2]{};
  ASSERT_TRUE(bool(engine.prepare_rejection(rejected)));
  const uint64_t reserved = watermark;
  EXPECT_EQ(engine.admit(rejected, input(&cb[1], 8)).error(), EINVAL);
  EXPECT_EQ(watermark, reserved);
  ASSERT_TRUE(bool(pending.resources.capture(7)));
  ASSERT_TRUE(bool(rejected.resources.capture(8)));
  ASSERT_TRUE(bool(engine.admit(pending, input(&cb[0], 7))));
  EXPECT_EQ(engine.admit(rejected, input(nullptr, 8)).error(), EINVAL);
  EXPECT_EQ(engine.admit(rejected, input(&cb[0], 8)).error(), EBUSY);
  EXPECT_EQ(engine.admit_rejected(rejected, input(nullptr), EIO).error(),
            EINVAL);
  EXPECT_EQ(engine.admit_rejected(rejected, input(&cb[1]), 0).error(), EINVAL);
  EXPECT_EQ(engine.admit_rejected(rejected, input(&cb[1]), EINPROGRESS).error(),
            EINVAL);
  EXPECT_EQ(engine.admit_rejected(rejected, input(&cb[0]), EIO).error(), EBUSY);
  EXPECT_EQ(watermark, UINT64_MAX - 1);
  // The last original token is burned, but dependency admission fails and
  // rolls back its CQ credit. The earlier local identity must survive both.
  EXPECT_EQ(engine.admit(rejected, input(&cb[1], 8)).error(), EAGAIN);
  EXPECT_EQ(watermark, UINT64_MAX);
  EXPECT_EQ(engine.admit(rejected, input(&cb[1], 8)).error(), EOVERFLOW);
  ASSERT_TRUE(bool(engine.admit_rejected(rejected, input(&cb[1]), EAGAIN)));
  EXPECT_EQ(rejected.state().identity().token, reserved);
  ASSERT_TRUE(bool(engine.take_result(&cb[1])));
  ASSERT_TRUE(engine.retire(rejected));
  ASSERT_TRUE(engine.cancel_queued(pending));
  ASSERT_TRUE(bool(engine.take_result(&cb[0])));
  ASSERT_TRUE(engine.retire(pending));
  EXPECT_EQ(ring.calls, 0U);
  EXPECT_TRUE(engine.empty());
}

TEST(LlvmLibcAioEngineListTest, PreparationIsEngineBoundAndSingleUse) {
  Ring ring, other_ring;
  uint64_t watermark = 0, other_watermark = 0;
  Engine engine(ring, watermark, 1, 1);
  Engine other(other_ring, other_watermark, 1, 1);
  ASSERT_TRUE(bool(engine.initialize(2)));
  ASSERT_TRUE(bool(other.initialize(2)));
  Engine::Request rejected(ring), accepted(ring);
  int cb[2]{};
  ASSERT_TRUE(bool(engine.prepare_rejection(rejected)));
  ASSERT_TRUE(bool(rejected.resources.capture(7)));
  EXPECT_EQ(engine.prepare_rejection(rejected).error(), EINVAL);
  EXPECT_EQ(other.prepare_rejection(rejected).error(), EINVAL);
  EXPECT_EQ(other.admit(rejected, input(&cb[0], 7)).error(), EINVAL);
  EXPECT_EQ(other.admit_rejected(rejected, input(&cb[0]), EIO).error(), EINVAL);
  EXPECT_EQ(watermark, uint64_t(1));
  EXPECT_EQ(other_watermark, uint64_t(0));
  EXPECT_TRUE(other.empty());
  ASSERT_TRUE(bool(engine.admit_rejected(rejected, input(&cb[0]), EIO)));
  EXPECT_EQ(rejected.state().identity().token, uint64_t(1));
  EXPECT_EQ(engine.prepare_rejection(rejected).error(), EINVAL);
  EXPECT_EQ(engine.admit_rejected(rejected, input(&cb[0]), EBADF).error(),
            EINVAL);
  EXPECT_EQ(engine.admit(rejected, input(&cb[0], 7)).error(), EINVAL);
  EXPECT_EQ(*engine.error(&cb[0]), EIO);
  ASSERT_TRUE(bool(engine.take_result(&cb[0])));
  ASSERT_TRUE(engine.retire(rejected));
  EXPECT_EQ(engine.prepare_rejection(rejected).error(), EINVAL);
  EXPECT_EQ(engine.admit_rejected(rejected, input(&cb[0]), EIO).error(),
            EINVAL);
  ASSERT_TRUE(bool(engine.prepare_rejection(accepted)));
  ASSERT_TRUE(bool(accepted.resources.capture(8)));
  ASSERT_TRUE(bool(engine.admit(accepted, input(&cb[1], 8))));
  EXPECT_EQ(accepted.state().identity().token, uint64_t(3));
  EXPECT_EQ(engine.prepare_rejection(accepted).error(), EINVAL);
  EXPECT_EQ(engine.admit_rejected(accepted, input(&cb[1]), EIO).error(),
            EINVAL);
  EXPECT_EQ(engine.admit(accepted, input(&cb[1], 8)).error(), EINVAL);
  EXPECT_EQ(*engine.error(&cb[1]), EINPROGRESS);
  ASSERT_TRUE(engine.cancel_queued(accepted));
  ASSERT_TRUE(bool(engine.take_result(&cb[1])));
  ASSERT_TRUE(engine.retire(accepted));
  EXPECT_EQ(engine.prepare_rejection(accepted).error(), EINVAL);
  EXPECT_EQ(engine.admit_rejected(accepted, input(&cb[1]), EIO).error(),
            EINVAL);
  EXPECT_EQ(watermark, uint64_t(3));
  EXPECT_EQ(ring.calls, 0U);
  EXPECT_EQ(other_ring.calls, 0U);
}

TEST(LlvmLibcAioEngineListTest, AbandonedPreparationNeedsNoEngineRetirement) {
  Ring ring;
  uint64_t watermark = UINT64_MAX - 2;
  unsigned wakes = 0;
  auto wake = [](void *context, const void *) {
    ++*static_cast<unsigned *>(context);
  };
  Engine::Request survivor(ring);
  {
    Engine engine(ring, watermark, 1, 1, wake, &wakes);
    Engine::Request abandoned(ring), exhausted(ring);
    EXPECT_EQ(engine.prepare_rejection(abandoned).error(), EINVAL);
    EXPECT_EQ(watermark, UINT64_MAX - 2);
    ASSERT_TRUE(bool(engine.initialize(2)));
    ASSERT_TRUE(bool(engine.prepare_rejection(abandoned)));
    ASSERT_TRUE(bool(engine.prepare_rejection(survivor)));
    EXPECT_EQ(watermark, UINT64_MAX);
    EXPECT_EQ(engine.prepare_rejection(exhausted).error(), EOVERFLOW);
    EXPECT_FALSE(abandoned.complete());
    EXPECT_FALSE(abandoned.published());
    EXPECT_FALSE(engine.cancel_queued(abandoned));
    EXPECT_FALSE(engine.retire(abandoned));
    EXPECT_TRUE(engine.empty());
    ASSERT_TRUE(bool(abandoned.resources.capture(7)));
    ASSERT_TRUE(bool(abandoned.resources.release()));
  }
  // Abandonment never refunds identities, including across drained restarts.
  // survivor is only destroyed, never used with a replacement engine.
  {
    Engine replacement(ring, watermark, 1, 1, wake, &wakes);
    Engine::Request fresh(ring);
    ASSERT_TRUE(bool(replacement.initialize(2)));
    EXPECT_EQ(replacement.prepare_rejection(fresh).error(), EOVERFLOW);
  }
  EXPECT_EQ(wakes, 0U);
  EXPECT_EQ(ring.calls, 0U);
  EXPECT_EQ(watermark, UINT64_MAX);
  EXPECT_TRUE(survivor.resources.drained());
}

TEST(LlvmLibcAioEngineListTest, LocalIdentitiesRemainDistinctAtFullCqBudget) {
  using Queue = LIBC_NAMESPACE::AioCompletionQueue<Ring>;
  Ring ring;
  uint64_t watermark = 0;
  Queue queue(ring, watermark);
  ASSERT_TRUE(bool(queue.initialize(1)));
  Queue::Entry original, next;
  auto first = queue.prepare(original, Queue::Kind::Transfer, nullptr, 1);
  ASSERT_TRUE(bool(first));
  EXPECT_EQ(queue.available(), 0U);
  auto local = queue.reserve_local_token();
  ASSERT_TRUE(bool(local));
  EXPECT_GT(*local, *first);
  EXPECT_EQ(queue.available(), 0U);
  EXPECT_EQ(queue.outstanding(), 1U);
  ASSERT_TRUE(queue.rollback(original));
  auto later = queue.prepare(next, Queue::Kind::Transfer, nullptr, 1);
  ASSERT_TRUE(bool(later));
  EXPECT_GT(*later, *local);
  ASSERT_TRUE(queue.rollback(next));
}
