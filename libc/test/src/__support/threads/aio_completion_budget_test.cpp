//===-- Tests for private AIO completion credits ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/aio_completion_budget.h"
#include "test/UnitTest/Test.h"

using Budget = LIBC_NAMESPACE::AioCompletionBudget;
using Credit = Budget::Credit;
using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;

namespace {
template <typename T> void expect_error(ErrorOr<T> result, int error) {
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error);
}

void expect_counts(const Budget &budget, uint32_t outstanding,
                   uint32_t available) {
  EXPECT_EQ(budget.outstanding(), outstanding);
  EXPECT_EQ(budget.available(), available);
  EXPECT_EQ(budget.capacity(), outstanding + available);
}

// Deterministic model of Ring's publication contract, not a kernel transport
// test. enqueue errors publish nothing; submit consumes only a prefix and its
// result cannot undo publication. CQ removal is modeled explicitly by consume.
struct SubmissionQueue {
  uint64_t tokens[2] = {};
  unsigned head = 0;
  unsigned tail = 0;

  ErrorOr<void> enqueue(uint64_t token) {
    if (tail - head == 2)
      return Error(EAGAIN);
    tokens[tail++ % 2] = token;
    return {};
  }

  ErrorOr<unsigned> submit(unsigned progress, int error = 0) {
    if (error)
      return Error(error);
    unsigned count = tail - head;
    unsigned submitted = progress < count ? progress : count;
    head += submitted;
    return submitted;
  }
};

ErrorOr<void> publish(Budget &budget, Credit &credit, SubmissionQueue &queue) {
  auto result = queue.enqueue(credit.token());
  if (!result) {
    EXPECT_TRUE(budget.rollback(credit));
    return Error(result.error());
  }
  EXPECT_TRUE(budget.mark_published(credit));
  return {};
}
} // namespace

TEST(LlvmLibcAioCompletionBudgetTest, CapacityGeometryAndOneTimeConfiguration) {
  Budget budget;
  Credit credit;
  expect_counts(budget, 0, 0);
  expect_error(budget.reserve(credit), EINVAL);
  expect_error(budget.initialize(0), EINVAL);
  expect_error(budget.initialize((uint32_t(1) << 30) + 1), EINVAL);
  expect_error(budget.initialize(uint32_t(1) << 31), EINVAL);
  expect_error(budget.initialize(UINT32_MAX), EINVAL);
  EXPECT_TRUE(credit.reclaimable());
  EXPECT_EQ(budget.last_issued_token(), uint64_t(0));
  expect_counts(budget, 0, 0);

  // A request for three SQ entries may yield SQ=4/CQ=4 or larger. Three
  // credits are safe without knowing either negotiated power-of-two count.
  ASSERT_TRUE(budget.initialize(3).has_value());
  expect_counts(budget, 0, 3);
  expect_error(budget.initialize(4), EBUSY);
  ASSERT_TRUE(budget.reserve(credit).has_value());
  expect_error(budget.initialize(1), EBUSY);
  ASSERT_TRUE(budget.rollback(credit));
  expect_error(budget.initialize(3), EBUSY);
  expect_counts(budget, 0, 3);

  Budget largest;
  ASSERT_TRUE(largest.initialize(uint32_t(1) << 30).has_value());
  ASSERT_TRUE(largest.reserve(credit).has_value());
  expect_counts(largest, 1, (uint32_t(1) << 30) - 1);
  ASSERT_TRUE(largest.rollback(credit));
}

TEST(LlvmLibcAioCompletionBudgetTest, QueueFullAndRollbackBurnsToken) {
  Budget budget;
  Credit first, second, excess;
  ASSERT_TRUE(budget.initialize(2).has_value());
  auto first_token = budget.reserve(first);
  ASSERT_TRUE(first_token.has_value());
  EXPECT_EQ(*first_token, uint64_t(1));
  auto second_token = budget.reserve(second);
  ASSERT_TRUE(second_token.has_value());
  EXPECT_EQ(*second_token, uint64_t(2));
  EXPECT_EQ(first.token(), *first_token);
  EXPECT_FALSE(first.reclaimable());
  expect_counts(budget, 2, 0);
  expect_error(budget.reserve(excess), EAGAIN);
  expect_error(budget.reserve(first), EINVAL);
  EXPECT_TRUE(excess.reclaimable());
  EXPECT_EQ(excess.token(), uint64_t(0));
  EXPECT_EQ(budget.last_issued_token(), uint64_t(2));
  EXPECT_FALSE(budget.consume(first, *first_token));
  ASSERT_TRUE(budget.rollback(first));
  EXPECT_FALSE(budget.rollback(first));
  EXPECT_FALSE(budget.mark_published(first));
  EXPECT_EQ(first.token(), uint64_t(0));
  expect_counts(budget, 1, 1);
  auto replacement = budget.reserve(first);
  ASSERT_TRUE(replacement.has_value());
  EXPECT_EQ(*replacement, uint64_t(3));
  ASSERT_TRUE(budget.mark_published(first));
  EXPECT_FALSE(budget.consume(first, *first_token));
  EXPECT_FALSE(budget.rollback(first));
  expect_counts(budget, 2, 0);
  ASSERT_TRUE(budget.consume(first, *replacement));
  ASSERT_TRUE(budget.rollback(second));
  expect_counts(budget, 0, 2);
}

TEST(LlvmLibcAioCompletionBudgetTest, CancelAndControlCreditsAreIndependent) {
  constexpr bool BOTH_ORDERS[] = {false, true};
  for (bool original_first : BOTH_ORDERS) {
    Budget budget;
    Credit original, cancel, control, blocked;
    ASSERT_TRUE(budget.initialize(3).has_value());
    ASSERT_TRUE(budget.reserve(original).has_value());
    ASSERT_TRUE(budget.reserve(cancel).has_value());
    ASSERT_TRUE(budget.reserve(control).has_value());
    uint64_t original_token = original.token();
    uint64_t cancel_token = cancel.token();
    uint64_t control_token = control.token();
    EXPECT_NE(original_token, cancel_token);
    EXPECT_NE(original_token, control_token);
    EXPECT_NE(cancel_token, control_token);
    ASSERT_TRUE(budget.mark_published(original));
    ASSERT_TRUE(budget.mark_published(cancel));
    ASSERT_TRUE(budget.mark_published(control));
    expect_error(budget.reserve(blocked), EAGAIN);
    EXPECT_FALSE(budget.consume(original, cancel_token));
    EXPECT_FALSE(budget.consume(cancel, original_token));
    expect_counts(budget, 3, 0);

    if (original_first) {
      ASSERT_TRUE(budget.consume(original, original_token));
      EXPECT_FALSE(cancel.reclaimable());
    } else {
      ASSERT_TRUE(budget.consume(cancel, cancel_token));
      EXPECT_FALSE(original.reclaimable());
    }
    expect_counts(budget, 2, 1);
    ASSERT_TRUE(budget.reserve(blocked).has_value());
    expect_counts(budget, 3, 0);
    if (original_first)
      ASSERT_TRUE(budget.consume(cancel, cancel_token));
    else
      ASSERT_TRUE(budget.consume(original, original_token));
    expect_counts(budget, 2, 1);
    EXPECT_FALSE(control.reclaimable());
    ASSERT_TRUE(budget.consume(control, control_token));
    ASSERT_TRUE(budget.rollback(blocked));
    expect_counts(budget, 0, 3);
  }
}

TEST(LlvmLibcAioCompletionBudgetTest, PartialPublicationAndShortSubmission) {
  Budget budget;
  SubmissionQueue queue;
  Credit first, second, unpublished, retry;
  // CQ capacity can exceed SQ capacity; admission and publication pressure
  // are distinct. A failed enqueue refunds only the unpublished reservation.
  ASSERT_TRUE(budget.initialize(4).has_value());
  ASSERT_TRUE(budget.reserve(first).has_value());
  ASSERT_TRUE(budget.reserve(second).has_value());
  ASSERT_TRUE(budget.reserve(unpublished).has_value());
  uint64_t first_token = first.token();
  uint64_t second_token = second.token();
  uint64_t abandoned_token = unpublished.token();
  ASSERT_TRUE(publish(budget, first, queue).has_value());
  ASSERT_TRUE(publish(budget, second, queue).has_value());
  expect_error(publish(budget, unpublished, queue), EAGAIN);
  EXPECT_TRUE(unpublished.reclaimable());
  EXPECT_EQ(queue.tail, 2U);
  EXPECT_EQ(queue.tokens[0], first_token);
  EXPECT_EQ(queue.tokens[1], second_token);
  expect_counts(budget, 2, 2);

  auto short_submit = queue.submit(1);
  ASSERT_TRUE(short_submit.has_value());
  EXPECT_EQ(*short_submit, 1U);
  EXPECT_FALSE(budget.rollback(first));
  EXPECT_FALSE(budget.rollback(second));
  EXPECT_FALSE(budget.mark_published(second));
  auto zero_submit = queue.submit(0);
  ASSERT_TRUE(zero_submit.has_value());
  EXPECT_EQ(*zero_submit, 0U);
  expect_error(queue.submit(0, EINTR), EINTR);
  expect_error(queue.submit(0, EIO), EIO);
  EXPECT_FALSE(budget.rollback(first));
  EXPECT_FALSE(budget.rollback(second));
  expect_counts(budget, 2, 2);

  ASSERT_TRUE(budget.reserve(retry).has_value());
  EXPECT_GT(retry.token(), abandoned_token);
  uint64_t retry_token = retry.token();
  ASSERT_TRUE(publish(budget, retry, queue).has_value());
  EXPECT_EQ(queue.tail, 3U);
  EXPECT_EQ(queue.tokens[1], second_token);
  auto rest = queue.submit(2);
  ASSERT_TRUE(rest.has_value());
  EXPECT_EQ(*rest, 2U);
  EXPECT_EQ(queue.head, queue.tail);
  // An empty SQ or even a ready CQE refunds nothing until CQ consumption.
  expect_counts(budget, 3, 1);
  ASSERT_TRUE(budget.consume(second, second_token));
  ASSERT_TRUE(budget.consume(retry, retry_token));
  ASSERT_TRUE(budget.consume(first, first_token));
  expect_counts(budget, 0, 4);
}

TEST(LlvmLibcAioCompletionBudgetTest, EmptySqDoesNotReturnCompletionCredits) {
  Budget budget;
  SubmissionQueue queue;
  Credit first, second, cancel;
  ASSERT_TRUE(budget.initialize(2).has_value());
  ASSERT_TRUE(budget.reserve(first).has_value());
  ASSERT_TRUE(budget.reserve(second).has_value());
  uint64_t first_token = first.token();
  uint64_t second_token = second.token();
  ASSERT_TRUE(publish(budget, first, queue).has_value());
  ASSERT_TRUE(publish(budget, second, queue).has_value());
  auto submitted = queue.submit(2);
  ASSERT_TRUE(submitted.has_value());
  EXPECT_EQ(*submitted, 2U);
  EXPECT_EQ(queue.head, queue.tail);
  // Both original CQEs may already be waiting in the CQ. An empty SQ cannot
  // justify another original or cancel SQE until one CQE is actually consumed.
  expect_error(budget.reserve(cancel), EAGAIN);
  expect_counts(budget, 2, 0);
  ASSERT_TRUE(budget.consume(first, first_token));
  ASSERT_TRUE(budget.reserve(cancel).has_value());
  uint64_t cancel_token = cancel.token();
  ASSERT_TRUE(publish(budget, cancel, queue).has_value());
  expect_counts(budget, 2, 0);
  submitted = queue.submit(1);
  ASSERT_TRUE(submitted.has_value());
  EXPECT_EQ(*submitted, 1U);
  ASSERT_TRUE(budget.consume(cancel, cancel_token));
  EXPECT_FALSE(second.reclaimable());
  expect_counts(budget, 1, 1);
  ASSERT_TRUE(budget.consume(second, second_token));
  expect_counts(budget, 0, 2);
}

TEST(LlvmLibcAioCompletionBudgetTest,
     WrongOwnerWrongTokenAndDuplicateCompletion) {
  Budget budget, foreign;
  Credit credit, other, empty;
  ASSERT_TRUE(budget.initialize(1).has_value());
  ASSERT_TRUE(foreign.initialize(1).has_value());
  ASSERT_TRUE(budget.reserve(credit).has_value());
  ASSERT_TRUE(foreign.reserve(other).has_value());
  uint64_t old_token = credit.token();
  // Equal numeric tokens in different ring namespaces do not share ownership.
  EXPECT_EQ(old_token, other.token());
  expect_error(foreign.reserve(credit), EINVAL);
  EXPECT_FALSE(foreign.rollback(credit));
  EXPECT_FALSE(foreign.mark_published(credit));
  EXPECT_FALSE(budget.consume(credit, old_token));
  EXPECT_FALSE(budget.consume(empty, 0));
  EXPECT_FALSE(budget.rollback(empty));
  EXPECT_FALSE(budget.mark_published(empty));
  ASSERT_TRUE(budget.mark_published(credit));
  ASSERT_TRUE(foreign.mark_published(other));
  EXPECT_FALSE(foreign.consume(credit, old_token));
  EXPECT_FALSE(budget.consume(other, old_token));
  EXPECT_FALSE(budget.consume(credit, 0));
  EXPECT_FALSE(budget.consume(credit, old_token + 1));
  expect_counts(budget, 1, 0);
  expect_counts(foreign, 1, 0);
  ASSERT_TRUE(budget.consume(credit, old_token));
  EXPECT_FALSE(budget.consume(credit, old_token));
  expect_counts(budget, 0, 1);
  ASSERT_TRUE(budget.reserve(credit).has_value());
  uint64_t new_token = credit.token();
  EXPECT_GT(new_token, old_token);
  ASSERT_TRUE(budget.mark_published(credit));
  EXPECT_FALSE(budget.consume(credit, old_token));
  expect_counts(budget, 1, 0);
  ASSERT_TRUE(budget.consume(credit, new_token));
  ASSERT_TRUE(foreign.consume(other, old_token));
  expect_counts(budget, 0, 1);
  expect_counts(foreign, 0, 1);
}

TEST(LlvmLibcAioCompletionBudgetTest, TokenExhaustionNeverWrapsOrReuses) {
  constexpr bool ROLLBACK_LAST[] = {false, true};
  for (bool rollback_last : ROLLBACK_LAST) {
    Budget budget(UINT64_MAX - 1);
    Credit last, blocked;
    ASSERT_TRUE(budget.initialize(2).has_value());
    auto token = budget.reserve(last);
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(*token, UINT64_MAX);
    EXPECT_EQ(budget.last_issued_token(), UINT64_MAX);
    expect_error(budget.reserve(blocked), EOVERFLOW);
    EXPECT_TRUE(blocked.reclaimable());
    EXPECT_EQ(blocked.token(), uint64_t(0));
    expect_counts(budget, 1, 1);
    if (rollback_last) {
      ASSERT_TRUE(budget.rollback(last));
    } else {
      ASSERT_TRUE(budget.mark_published(last));
      ASSERT_TRUE(budget.consume(last, UINT64_MAX));
    }
    expect_error(budget.reserve(last), EOVERFLOW);
    expect_counts(budget, 0, 2);
    EXPECT_TRUE(last.reclaimable());
    EXPECT_EQ(budget.last_issued_token(), UINT64_MAX);

    Budget restored(budget.last_issued_token());
    ASSERT_TRUE(restored.initialize(1).has_value());
    expect_error(restored.reserve(blocked), EOVERFLOW);
    expect_counts(restored, 0, 1);
  }
}
