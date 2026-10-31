//===-- Tests for private AIO original-fd dependencies
//---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/aio_file_dependencies.h"
#include "test/UnitTest/Test.h"

using Dependencies = LIBC_NAMESPACE::AioFileDependencies;
using Record = Dependencies::Record;
using Kind = Dependencies::Kind;
using Decision = Dependencies::Decision;
using LIBC_NAMESPACE::ErrorOr;

namespace {
constexpr int FD = 7;
constexpr int OTHER = 8;
constexpr bool BOTH_ORDERS[] = {false, true};

template <typename T> void expect_error(ErrorOr<T> result, int error) {
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error);
}

void expect_decision(Dependencies &dependencies, Record &sync,
                     Decision decision) {
  auto result = dependencies.decide(sync, sync.order());
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, decision);
  EXPECT_EQ(sync.decision_final(), decision == Decision::Ready);
}

void admit_record(Dependencies &dependencies, Record &record, Kind kind,
                  int fd = FD) {
  auto order = dependencies.admit(record, kind, fd);
  ASSERT_TRUE(order.has_value());
  EXPECT_EQ(record.order(), *order);
}
} // namespace

TEST(LlvmLibcAioFileDependenciesTest, SameOriginalFdWaitsForEveryPredecessor) {
  for (bool first_completes_first : BOTH_ORDERS) {
    Dependencies dependencies(3);
    Record first, second, sync;
    admit_record(dependencies, first, Kind::Operation);
    admit_record(dependencies, second, Kind::Operation);
    admit_record(dependencies, sync, Kind::Sync);
    expect_decision(dependencies, sync, Decision::Wait);
    Record &early = first_completes_first ? first : second;
    Record &late = first_completes_first ? second : first;
    ASSERT_TRUE(dependencies.complete(early, early.order()));
    expect_decision(dependencies, sync, Decision::Wait);
    ASSERT_TRUE(dependencies.complete(late, late.order()));
    expect_decision(dependencies, sync, Decision::Ready);
    EXPECT_FALSE(dependencies.release(sync, sync.order()));
    ASSERT_TRUE(dependencies.complete(sync, sync.order()));
    ASSERT_TRUE(dependencies.release(first, first.order()));
    ASSERT_TRUE(dependencies.release(second, second.order()));
    ASSERT_TRUE(dependencies.release(sync, sync.order()));
    EXPECT_EQ(dependencies.outstanding(), uint32_t(0));
  }
}

TEST(LlvmLibcAioFileDependenciesTest,
     ExecutionReadinessIgnoresErrorsWithoutConsumingSyncSnapshot) {
  Dependencies dependencies(5);
  Record first, second, sync, later, other;
  admit_record(dependencies, first, Kind::Operation);
  admit_record(dependencies, second, Kind::Operation);
  admit_record(dependencies, sync, Kind::Sync);
  admit_record(dependencies, later, Kind::Operation);
  admit_record(dependencies, other, Kind::Operation, OTHER);
  EXPECT_EQ(*dependencies.ready_to_publish(first, first.order()),
            Decision::Ready);
  EXPECT_EQ(*dependencies.ready_to_publish(second, second.order()),
            Decision::Wait);
  EXPECT_EQ(*dependencies.ready_to_publish(other, other.order()),
            Decision::Ready);
  ASSERT_TRUE(dependencies.complete(first, first.order(), ENOSPC));
  EXPECT_FALSE(dependencies.release(first, first.order()));
  EXPECT_EQ(*dependencies.ready_to_publish(second, second.order()),
            Decision::Ready);
  EXPECT_FALSE(second.decision_final());
  EXPECT_EQ(*dependencies.ready_to_publish(sync, sync.order()), Decision::Wait);
  ASSERT_TRUE(dependencies.complete(second, second.order(), EIO));
  for (unsigned i = 0; i != 2; ++i) {
    EXPECT_EQ(*dependencies.ready_to_publish(sync, sync.order()),
              Decision::Ready);
    EXPECT_FALSE(sync.decision_final());
  }
  expect_error(dependencies.decide(sync, sync.order()), ENOSPC);
  // A final sync decision is not the sync's original terminal event.
  EXPECT_EQ(*dependencies.ready_to_publish(later, later.order()),
            Decision::Wait);
  ASSERT_TRUE(dependencies.complete(sync, sync.order(), ENOSPC));
  EXPECT_EQ(*dependencies.ready_to_publish(later, later.order()),
            Decision::Ready);
  ASSERT_TRUE(dependencies.release(first, first.order()));
  ASSERT_TRUE(dependencies.release(second, second.order()));
  ASSERT_TRUE(dependencies.release(sync, sync.order()));
  ASSERT_TRUE(dependencies.complete(later, later.order()));
  ASSERT_TRUE(dependencies.complete(other, other.order()));
  ASSERT_TRUE(dependencies.release(later, later.order()));
  ASSERT_TRUE(dependencies.release(other, other.order()));
}

TEST(LlvmLibcAioFileDependenciesTest,
     ExecutionReadinessChecksAllPredecessorsAndRecordIdentity) {
  Dependencies dependencies(3), foreign(1);
  Record first, canceled, later, empty;
  admit_record(dependencies, first, Kind::Operation);
  admit_record(dependencies, canceled, Kind::Sync);
  admit_record(dependencies, later, Kind::Operation);
  expect_error(dependencies.ready_to_publish(empty, 0), EINVAL);
  expect_error(foreign.ready_to_publish(first, first.order()), EINVAL);
  expect_error(dependencies.ready_to_publish(first, 0), EINVAL);
  expect_error(dependencies.ready_to_publish(first, first.order() + 1), EINVAL);
  ASSERT_TRUE(dependencies.abandon_sync(canceled, canceled.order(), ECANCELED));
  EXPECT_EQ(*dependencies.ready_to_publish(later, later.order()),
            Decision::Wait);
  ASSERT_TRUE(dependencies.complete(first, first.order(), ECANCELED));
  expect_error(dependencies.ready_to_publish(first, first.order()), EINVAL);
  EXPECT_EQ(*dependencies.ready_to_publish(later, later.order()),
            Decision::Ready);
  const uint64_t old_order = first.order();
  ASSERT_TRUE(dependencies.release(first, old_order));
  admit_record(dependencies, first, Kind::Operation);
  expect_error(dependencies.ready_to_publish(first, old_order), EINVAL);
  EXPECT_EQ(*dependencies.ready_to_publish(first, first.order()),
            Decision::Wait);
  ASSERT_TRUE(dependencies.complete(later, later.order()));
  EXPECT_EQ(*dependencies.ready_to_publish(first, first.order()),
            Decision::Ready);
}

TEST(LlvmLibcAioFileDependenciesTest, DifferentNumericAliasesDoNotGroup) {
  Dependencies dependencies(2);
  Record alias, sync;
  // Even if the caller obtained OTHER by dup(FD), it is a different group.
  admit_record(dependencies, alias, Kind::Operation, OTHER);
  admit_record(dependencies, sync, Kind::Sync);
  expect_decision(dependencies, sync, Decision::Ready);
  EXPECT_FALSE(dependencies.release(alias, alias.order()));
  ASSERT_TRUE(dependencies.complete(alias, alias.order(), EIO));
  ASSERT_TRUE(dependencies.release(alias, alias.order()));
}

TEST(LlvmLibcAioFileDependenciesTest,
     UnrelatedSyncDoesNotRetainTerminalRecord) {
  Dependencies dependencies(3);
  Record other, pending, sync;
  admit_record(dependencies, other, Kind::Operation, OTHER);
  admit_record(dependencies, pending, Kind::Operation);
  admit_record(dependencies, sync, Kind::Sync);
  ASSERT_TRUE(dependencies.complete(other, other.order(), EIO));
  expect_decision(dependencies, sync, Decision::Wait);
  ASSERT_TRUE(dependencies.release(other, other.order()));
  ASSERT_TRUE(dependencies.complete(pending, pending.order()));
  expect_decision(dependencies, sync, Decision::Ready);
}

TEST(LlvmLibcAioFileDependenciesTest, LaterWorkNeverEntersSnapshot) {
  Dependencies dependencies(5);
  Record prior, sync, later_same, later_other, later_sync;
  admit_record(dependencies, prior, Kind::Operation);
  admit_record(dependencies, sync, Kind::Sync);
  admit_record(dependencies, later_same, Kind::Operation);
  admit_record(dependencies, later_other, Kind::Operation, OTHER);
  admit_record(dependencies, later_sync, Kind::Sync);
  ASSERT_TRUE(dependencies.complete(later_same, later_same.order(), EIO));
  expect_decision(dependencies, sync, Decision::Wait);
  ASSERT_TRUE(dependencies.complete(prior, prior.order()));
  expect_decision(dependencies, sync, Decision::Ready);
  ASSERT_TRUE(dependencies.complete(sync, sync.order()));
  expect_error(dependencies.decide(later_sync, later_sync.order()), EIO);
}

TEST(LlvmLibcAioFileDependenciesTest, ZeroFdIsValid) {
  Dependencies dependencies(2);
  Record prior, sync;
  admit_record(dependencies, prior, Kind::Operation, 0);
  admit_record(dependencies, sync, Kind::Sync, 0);
  expect_decision(dependencies, sync, Decision::Wait);
  ASSERT_TRUE(dependencies.complete(prior, prior.order()));
  expect_decision(dependencies, sync, Decision::Ready);
}

TEST(LlvmLibcAioFileDependenciesTest, NegativeFdRejectedWithoutMutation) {
  Dependencies dependencies(1);
  Record record;
  const Kind kinds[] = {Kind::Operation, Kind::Sync};
  for (Kind kind : kinds) {
    expect_error(dependencies.admit(record, kind, -1), EINVAL);
    expect_error(dependencies.admit(record, kind, -2), EINVAL);
    EXPECT_TRUE(record.reclaimable());
    EXPECT_EQ(record.order(), uint64_t(0));
    EXPECT_EQ(dependencies.outstanding(), uint32_t(0));
    EXPECT_EQ(dependencies.last_issued_order(), uint64_t(0));
  }
  admit_record(dependencies, record, Kind::Operation);
}

TEST(LlvmLibcAioFileDependenciesTest, InvalidOwnerOrderAndKind) {
  Dependencies dependencies(2), foreign(1);
  Record record, sync, other, empty;
  admit_record(dependencies, record, Kind::Operation);
  admit_record(foreign, other, Kind::Operation);
  EXPECT_EQ(record.order(), other.order());
  EXPECT_FALSE(foreign.complete(record, record.order()));
  EXPECT_FALSE(foreign.release(record, record.order()));
  expect_error(foreign.admit(record, Kind::Operation, FD), EINVAL);
  expect_error(dependencies.admit(empty, static_cast<Kind>(99), FD), EINVAL);
  EXPECT_TRUE(empty.reclaimable());
  EXPECT_FALSE(dependencies.complete(empty, 0));
  EXPECT_FALSE(dependencies.release(empty, 0));
  expect_error(dependencies.decide(record, record.order()), EINVAL);
  expect_error(dependencies.decide(empty, 0), EINVAL);
  admit_record(dependencies, sync, Kind::Sync);
  expect_error(foreign.decide(sync, sync.order()), EINVAL);
  expect_error(dependencies.decide(sync, 0), EINVAL);
  expect_error(dependencies.decide(sync, sync.order() + 1), EINVAL);
  EXPECT_FALSE(sync.decision_final());
  EXPECT_FALSE(dependencies.complete(sync, sync.order()));
  expect_decision(dependencies, sync, Decision::Wait);
  ASSERT_TRUE(dependencies.complete(record, record.order(), EINVAL));
  EXPECT_FALSE(dependencies.complete(record, record.order()));
  expect_error(dependencies.decide(sync, sync.order()), EINVAL);
  EXPECT_TRUE(sync.decision_final());
  expect_error(dependencies.decide(sync, sync.order()), EINVAL);
}

TEST(LlvmLibcAioFileDependenciesTest, SnapshotRetentionAndMultipleSyncs) {
  Dependencies dependencies(4);
  Record prior, first_sync, other, second_sync;
  admit_record(dependencies, prior, Kind::Operation);
  admit_record(dependencies, first_sync, Kind::Sync);
  admit_record(dependencies, other, Kind::Operation, OTHER);
  admit_record(dependencies, second_sync, Kind::Sync);
  ASSERT_TRUE(dependencies.complete(prior, prior.order()));
  ASSERT_TRUE(dependencies.complete(other, other.order()));
  EXPECT_FALSE(dependencies.release(prior, prior.order()));
  ASSERT_TRUE(dependencies.release(other, other.order()));
  expect_decision(dependencies, first_sync, Decision::Ready);
  EXPECT_FALSE(dependencies.release(prior, prior.order()));
  expect_decision(dependencies, second_sync, Decision::Wait);
  ASSERT_TRUE(dependencies.complete(first_sync, first_sync.order()));
  EXPECT_FALSE(dependencies.release(first_sync, first_sync.order()));
  expect_decision(dependencies, second_sync, Decision::Ready);
  ASSERT_TRUE(dependencies.release(prior, prior.order()));
  ASSERT_TRUE(dependencies.release(first_sync, first_sync.order()));
  ASSERT_TRUE(dependencies.complete(second_sync, second_sync.order()));
  ASSERT_TRUE(dependencies.release(second_sync, second_sync.order()));
  EXPECT_EQ(dependencies.outstanding(), uint32_t(0));
}

TEST(LlvmLibcAioFileDependenciesTest, CapacityReuseAndStaleOrders) {
  Dependencies dependencies(1), zero_capacity(0);
  Record record, excess;
  expect_error(zero_capacity.admit(excess, Kind::Operation, FD), EAGAIN);
  EXPECT_EQ(zero_capacity.last_issued_order(), uint64_t(0));
  admit_record(dependencies, record, Kind::Operation);
  uint64_t old_order = record.order();
  EXPECT_EQ(old_order, uint64_t(1));
  expect_error(dependencies.admit(record, Kind::Operation, FD), EINVAL);
  expect_error(dependencies.admit(excess, Kind::Operation, FD), EAGAIN);
  EXPECT_TRUE(excess.reclaimable());
  EXPECT_EQ(excess.order(), uint64_t(0));
  EXPECT_EQ(dependencies.last_issued_order(), old_order);
  ASSERT_TRUE(dependencies.complete(record, old_order));
  ASSERT_TRUE(dependencies.release(record, old_order));
  EXPECT_TRUE(record.reclaimable());
  EXPECT_EQ(record.order(), uint64_t(0));
  EXPECT_FALSE(dependencies.release(record, old_order));
  ASSERT_TRUE(dependencies.admit(record, Kind::Sync, FD).has_value());
  EXPECT_EQ(record.order(), old_order + 1);
  EXPECT_FALSE(record.decision_final());
  EXPECT_FALSE(dependencies.complete(record, old_order));
  EXPECT_FALSE(dependencies.release(record, old_order));
  expect_error(dependencies.decide(record, old_order), EINVAL);
  EXPECT_FALSE(record.decision_final());
  expect_decision(dependencies, record, Decision::Ready);
  expect_error(dependencies.decide(record, record.order()), EINVAL);
  ASSERT_TRUE(dependencies.complete(record, record.order()));
  ASSERT_TRUE(dependencies.release(record, record.order()));
}

TEST(LlvmLibcAioFileDependenciesTest, OrderExhaustionNeverWraps) {
  Dependencies dependencies(2, UINT64_MAX - 1);
  Record last, blocked;
  admit_record(dependencies, last, Kind::Operation);
  EXPECT_EQ(last.order(), UINT64_MAX);
  EXPECT_EQ(dependencies.last_issued_order(), UINT64_MAX);
  expect_error(dependencies.admit(blocked, Kind::Operation, FD), EOVERFLOW);
  EXPECT_TRUE(blocked.reclaimable());
  ASSERT_TRUE(dependencies.complete(last, last.order()));
  ASSERT_TRUE(dependencies.release(last, last.order()));
  expect_error(dependencies.admit(last, Kind::Operation, FD), EOVERFLOW);
  EXPECT_EQ(dependencies.outstanding(), uint32_t(0));
  EXPECT_EQ(dependencies.last_issued_order(), UINT64_MAX);
  Dependencies restored(1, dependencies.last_issued_order());
  expect_error(restored.admit(blocked, Kind::Sync, FD), EOVERFLOW);
  EXPECT_TRUE(blocked.reclaimable());
}

TEST(LlvmLibcAioFileDependenciesTest, SameFdFailureSurvivesResultRetirement) {
  for (bool first_completes_first : BOTH_ORDERS) {
    Dependencies dependencies(3);
    Record first, second, sync;
    admit_record(dependencies, first, Kind::Operation);
    admit_record(dependencies, second, Kind::Operation);
    admit_record(dependencies, sync, Kind::Sync);
    Record &early = first_completes_first ? first : second;
    Record &late = first_completes_first ? second : first;
    ASSERT_TRUE(dependencies.complete(early, early.order(),
                                      first_completes_first ? ENOSPC : EIO));
    // aio_return may retire its own reference, but not this snapshot's state.
    EXPECT_FALSE(dependencies.release(early, early.order()));
    expect_decision(dependencies, sync, Decision::Wait);
    ASSERT_TRUE(dependencies.complete(late, late.order(),
                                      first_completes_first ? EIO : ENOSPC));
    expect_error(dependencies.decide(sync, sync.order()), ENOSPC);
    EXPECT_TRUE(sync.decision_final());
    ASSERT_TRUE(dependencies.complete(sync, sync.order(), ENOSPC));
    ASSERT_TRUE(dependencies.release(first, first.order()));
    ASSERT_TRUE(dependencies.release(second, second.order()));
    ASSERT_TRUE(dependencies.release(sync, sync.order()));
  }
}

TEST(LlvmLibcAioFileDependenciesTest, EarlierTerminalFailureIsNotNewSyncWork) {
  Dependencies dependencies(2);
  Record prior, sync;
  admit_record(dependencies, prior, Kind::Operation);
  ASSERT_TRUE(dependencies.complete(prior, prior.order(), EIO));
  admit_record(dependencies, sync, Kind::Sync);
  // The application may still own prior's result; retaining a completed
  // result cannot turn it into newly queued work for a later sync.
  expect_decision(dependencies, sync, Decision::Ready);
}

TEST(LlvmLibcAioFileDependenciesTest, InvalidCompletionDoesNotEndDependency) {
  Dependencies dependencies(2);
  Record prior, sync;
  admit_record(dependencies, prior, Kind::Operation);
  admit_record(dependencies, sync, Kind::Sync);
  EXPECT_FALSE(dependencies.complete(prior, prior.order(), -EIO));
  EXPECT_FALSE(dependencies.complete(prior, prior.order(), EINPROGRESS));
  EXPECT_FALSE(dependencies.complete(prior, prior.order() + 1, EIO));
  expect_decision(dependencies, sync, Decision::Wait);
  ASSERT_TRUE(dependencies.complete(prior, prior.order(), EIO));
  EXPECT_FALSE(dependencies.complete(prior, prior.order(), ENOSPC));
  expect_error(dependencies.decide(sync, sync.order()), EIO);
}

TEST(LlvmLibcAioFileDependenciesTest,
     AbandonQueuedSyncReleasesOnlyItsSnapshot) {
  Dependencies dependencies(3);
  Record prior, sync, later;
  admit_record(dependencies, prior, Kind::Operation);
  admit_record(dependencies, sync, Kind::Sync);
  admit_record(dependencies, later, Kind::Sync);
  expect_decision(dependencies, sync, Decision::Wait);
  EXPECT_FALSE(dependencies.abandon_sync(prior, prior.order(), ECANCELED));
  EXPECT_FALSE(dependencies.abandon_sync(sync, sync.order() + 1, ECANCELED));
  EXPECT_FALSE(dependencies.abandon_sync(sync, sync.order(), 0));
  EXPECT_FALSE(dependencies.abandon_sync(sync, sync.order(), EINPROGRESS));
  ASSERT_TRUE(dependencies.abandon_sync(sync, sync.order(), ECANCELED));
  EXPECT_FALSE(dependencies.abandon_sync(sync, sync.order(), ECANCELED));
  EXPECT_FALSE(dependencies.release(sync, sync.order()));
  expect_decision(dependencies, later, Decision::Wait);
  ASSERT_TRUE(dependencies.complete(prior, prior.order()));
  expect_error(dependencies.decide(later, later.order()), ECANCELED);
  ASSERT_TRUE(dependencies.complete(later, later.order(), ECANCELED));
  ASSERT_TRUE(dependencies.release(prior, prior.order()));
  ASSERT_TRUE(dependencies.release(sync, sync.order()));
  ASSERT_TRUE(dependencies.release(later, later.order()));
}
