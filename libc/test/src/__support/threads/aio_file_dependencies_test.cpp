//===-- Tests for private AIO same-file dependencies ---------------------===//
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
using Identity = Dependencies::Identity;
using FileType = Dependencies::FileType;
using IdentityState = Dependencies::IdentityState;
using Decision = Dependencies::Decision;
using LIBC_NAMESPACE::ErrorOr;

namespace {
constexpr Identity REGULAR = {8, 1, UINT64_C(0x8123456789abcdef),
                              FileType::Regular};
constexpr Identity OTHER = {8, 1, REGULAR.inode + 1, FileType::Regular};
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

void admit_known(Dependencies &dependencies, Record &record, Kind kind,
                 Identity identity = REGULAR) {
  auto order = dependencies.admit(record, kind);
  ASSERT_TRUE(order.has_value());
  EXPECT_EQ(record.order(), *order);
  ASSERT_TRUE(dependencies.set_known(record, *order, identity));
}
} // namespace

// Synthetic, caller-qualified lookup results, not a STATX/filesystem test.
// Neither descriptor numbers nor open-description addresses enter the API.
TEST(LlvmLibcAioFileDependenciesTest, DupAndIndependentOpenShareIdentity) {
  for (bool alias_first : BOTH_ORDERS) {
    Dependencies dependencies(4);
    Record original, dup_alias, independent_open, sync;
    admit_known(dependencies, original, Kind::Operation);
    admit_known(dependencies, dup_alias, Kind::Operation);
    admit_known(dependencies, independent_open, Kind::Operation);
    admit_known(dependencies, sync, Kind::Sync);
    expect_decision(dependencies, sync, Decision::Wait);
    ASSERT_TRUE(dependencies.complete(original, original.order()));
    Record &first = alias_first ? dup_alias : independent_open;
    Record &last = alias_first ? independent_open : dup_alias;
    ASSERT_TRUE(dependencies.complete(first, first.order()));
    expect_decision(dependencies, sync, Decision::Wait);
    ASSERT_TRUE(dependencies.complete(last, last.order()));
    expect_decision(dependencies, sync, Decision::Ready);
    // Ready is not terminal completion of the actual sync operation.
    EXPECT_FALSE(dependencies.release(sync, sync.order()));
    ASSERT_TRUE(dependencies.complete(sync, sync.order()));
    ASSERT_TRUE(dependencies.release(original, original.order()));
    ASSERT_TRUE(dependencies.release(dup_alias, dup_alias.order()));
    ASSERT_TRUE(
        dependencies.release(independent_open, independent_open.order()));
    ASSERT_TRUE(dependencies.release(sync, sync.order()));
    EXPECT_EQ(dependencies.outstanding(), uint32_t(0));
  }
}

TEST(LlvmLibcAioFileDependenciesTest, DifferentDeviceInodeOrTypeDoesNotWait) {
  const Identity different[] = {
      {REGULAR.device_major + 1, REGULAR.device_minor, REGULAR.inode,
       FileType::Regular},
      {REGULAR.device_major, REGULAR.device_minor + 1, REGULAR.inode,
       FileType::Regular},
      OTHER,
      {REGULAR.device_major, REGULAR.device_minor, REGULAR.inode,
       FileType::Fifo},
  };
  for (Identity identity : different) {
    Dependencies dependencies(2);
    Record ongoing, sync;
    admit_known(dependencies, ongoing, Kind::Operation, identity);
    admit_known(dependencies, sync, Kind::Sync);
    expect_decision(dependencies, sync, Decision::Ready);
    EXPECT_FALSE(dependencies.release(ongoing, ongoing.order()));
  }
}

TEST(LlvmLibcAioFileDependenciesTest, PipeLookupFinishesWithoutPipeTransfer) {
  Dependencies dependencies(2);
  Record pipe_read, sync;
  ASSERT_TRUE(dependencies.admit(pipe_read, Kind::Operation).has_value());
  admit_known(dependencies, sync, Kind::Sync);
  expect_decision(dependencies, sync, Decision::Wait);
  // Caller-qualified full tuples differ in normalized type even with identical
  // numeric dev/inode fields. The pipe read is never completed here.
  Identity pipe = REGULAR;
  pipe.type = FileType::Fifo;
  ASSERT_TRUE(dependencies.set_known(pipe_read, pipe_read.order(), pipe));
  expect_decision(dependencies, sync, Decision::Ready);
  EXPECT_FALSE(dependencies.release(pipe_read, pipe_read.order()));
}

TEST(LlvmLibcAioFileDependenciesTest,
     PendingClassificationAndSameFileCompletion) {
  for (bool predecessor_first : BOTH_ORDERS) {
    Dependencies dependencies(2);
    Record prior, sync;
    ASSERT_TRUE(dependencies.admit(prior, Kind::Operation).has_value());
    ASSERT_TRUE(dependencies.admit(sync, Kind::Sync).has_value());
    EXPECT_EQ(prior.classification(), IdentityState::Pending);
    expect_decision(dependencies, sync, Decision::Wait);
    Record &first = predecessor_first ? prior : sync;
    Record &last = predecessor_first ? sync : prior;
    ASSERT_TRUE(dependencies.set_known(first, first.order(), REGULAR));
    expect_decision(dependencies, sync, Decision::Wait);
    ASSERT_TRUE(dependencies.set_known(last, last.order(), REGULAR));
    expect_decision(dependencies, sync, Decision::Wait);
    ASSERT_TRUE(dependencies.complete(prior, prior.order()));
    expect_decision(dependencies, sync, Decision::Ready);
  }
}

TEST(LlvmLibcAioFileDependenciesTest,
     PendingDifferentFileNeedsOnlyClassification) {
  Dependencies dependencies(2);
  Record prior, sync;
  ASSERT_TRUE(dependencies.admit(prior, Kind::Operation).has_value());
  admit_known(dependencies, sync, Kind::Sync);
  expect_decision(dependencies, sync, Decision::Wait);
  ASSERT_TRUE(dependencies.set_known(prior, prior.order(), OTHER));
  expect_decision(dependencies, sync, Decision::Ready);
  EXPECT_FALSE(dependencies.release(prior, prior.order()));
}

TEST(LlvmLibcAioFileDependenciesTest, IncompleteUnknownFailsSyncExplicitly) {
  const int errors[] = {EBADF, EIO, EINVAL, EOPNOTSUPP};
  for (int error : errors) {
    Dependencies dependencies(3);
    Record known_same, unknown, sync;
    admit_known(dependencies, known_same, Kind::Operation);
    ASSERT_TRUE(dependencies.admit(unknown, Kind::Operation).has_value());
    admit_known(dependencies, sync, Kind::Sync);
    ASSERT_TRUE(dependencies.set_unknown(unknown, unknown.order(), error));
    EXPECT_EQ(unknown.classification(), IdentityState::Unknown);
    // An unknown must not be hidden by either a same-file wait or a successful
    // metadata lookup on the sync itself.
    expect_error(dependencies.decide(sync, sync.order()), error);
    EXPECT_TRUE(sync.decision_final());
    expect_error(dependencies.decide(sync, sync.order()), EINVAL);
    ASSERT_TRUE(dependencies.complete(sync, sync.order()));
    ASSERT_TRUE(dependencies.release(sync, sync.order()));
    EXPECT_FALSE(unknown.reclaimable());
  }
}

TEST(LlvmLibcAioFileDependenciesTest,
     UnknownPredecessorFailsDespitePendingMetadata) {
  for (bool unknown_first : BOTH_ORDERS) {
    for (bool sync_known : BOTH_ORDERS) {
      Dependencies dependencies(3);
      Record first, second, sync;
      ASSERT_TRUE(dependencies.admit(first, Kind::Operation).has_value());
      ASSERT_TRUE(dependencies.admit(second, Kind::Operation).has_value());
      ASSERT_TRUE(dependencies.admit(sync, Kind::Sync).has_value());
      if (sync_known)
        ASSERT_TRUE(dependencies.set_known(sync, sync.order(), REGULAR));
      Record &unknown = unknown_first ? first : second;
      Record &pending = unknown_first ? second : first;
      ASSERT_TRUE(dependencies.set_unknown(unknown, unknown.order(), EBADF));
      expect_error(dependencies.decide(sync, sync.order()), EBADF);
      EXPECT_TRUE(sync.decision_final());
      EXPECT_EQ(pending.classification(), IdentityState::Pending);
      ASSERT_TRUE(dependencies.complete(sync, sync.order()));
      if (!sync_known) {
        EXPECT_FALSE(dependencies.release(sync, sync.order()));
        ASSERT_TRUE(dependencies.set_known(sync, sync.order(), REGULAR));
      }
      ASSERT_TRUE(dependencies.release(sync, sync.order()));
    }
  }
}

TEST(LlvmLibcAioFileDependenciesTest, TerminalPendingAndUnknownAreIrrelevant) {
  Dependencies dependencies(4);
  Record pending, unknown, same, sync;
  ASSERT_TRUE(dependencies.admit(pending, Kind::Operation).has_value());
  ASSERT_TRUE(dependencies.admit(unknown, Kind::Operation).has_value());
  ASSERT_TRUE(dependencies.set_unknown(unknown, unknown.order(), EIO));
  admit_known(dependencies, same, Kind::Operation);
  admit_known(dependencies, sync, Kind::Sync);
  ASSERT_TRUE(dependencies.complete(pending, pending.order()));
  ASSERT_TRUE(dependencies.complete(unknown, unknown.order()));
  expect_decision(dependencies, sync, Decision::Wait);
  ASSERT_TRUE(dependencies.complete(same, same.order()));
  expect_decision(dependencies, sync, Decision::Ready);
  ASSERT_TRUE(dependencies.release(unknown, unknown.order()));
  ASSERT_TRUE(dependencies.release(same, same.order()));
  // No ordering dependency remains, but async lookup storage is still owned.
  EXPECT_FALSE(dependencies.release(pending, pending.order()));
  ASSERT_TRUE(dependencies.set_unknown(pending, pending.order(), EBADF));
  ASSERT_TRUE(dependencies.release(pending, pending.order()));
}

TEST(LlvmLibcAioFileDependenciesTest, LaterWorkNeverEntersSnapshot) {
  Dependencies dependencies(6);
  Record prior, sync, later_same, later_pending, later_unknown, later_sync;
  admit_known(dependencies, prior, Kind::Operation, OTHER);
  admit_known(dependencies, sync, Kind::Sync);
  admit_known(dependencies, later_same, Kind::Operation);
  ASSERT_TRUE(dependencies.admit(later_pending, Kind::Operation).has_value());
  ASSERT_TRUE(dependencies.admit(later_unknown, Kind::Operation).has_value());
  ASSERT_TRUE(
      dependencies.set_unknown(later_unknown, later_unknown.order(), EIO));
  ASSERT_TRUE(dependencies.admit(later_sync, Kind::Sync).has_value());
  expect_decision(dependencies, sync, Decision::Ready);
}

TEST(LlvmLibcAioFileDependenciesTest, SnapshotRetentionAndMultipleSyncs) {
  Dependencies dependencies(4);
  Record prior, first_sync, other, second_sync;
  admit_known(dependencies, prior, Kind::Operation);
  admit_known(dependencies, first_sync, Kind::Sync);
  admit_known(dependencies, other, Kind::Operation, OTHER);
  admit_known(dependencies, second_sync, Kind::Sync);
  ASSERT_TRUE(dependencies.complete(prior, prior.order()));
  ASSERT_TRUE(dependencies.complete(other, other.order()));
  EXPECT_FALSE(dependencies.release(prior, prior.order()));
  EXPECT_FALSE(dependencies.release(other, other.order()));
  expect_decision(dependencies, first_sync, Decision::Ready);
  EXPECT_FALSE(dependencies.release(prior, prior.order()));
  expect_decision(dependencies, second_sync, Decision::Wait);
  ASSERT_TRUE(dependencies.complete(first_sync, first_sync.order()));
  EXPECT_FALSE(dependencies.release(first_sync, first_sync.order()));
  expect_decision(dependencies, second_sync, Decision::Ready);
  ASSERT_TRUE(dependencies.release(other, other.order()));
  ASSERT_TRUE(dependencies.release(prior, prior.order()));
  ASSERT_TRUE(dependencies.release(first_sync, first_sync.order()));
  ASSERT_TRUE(dependencies.complete(second_sync, second_sync.order()));
  ASSERT_TRUE(dependencies.release(second_sync, second_sync.order()));
  EXPECT_EQ(dependencies.outstanding(), uint32_t(0));
}

TEST(LlvmLibcAioFileDependenciesTest,
     UnknownSyncFailsDespitePendingPredecessor) {
  const int errors[] = {EBADF, EINVAL, EOPNOTSUPP};
  for (int error : errors) {
    Dependencies dependencies(2);
    Record prior, sync;
    ASSERT_TRUE(dependencies.admit(prior, Kind::Operation).has_value());
    ASSERT_TRUE(dependencies.admit(sync, Kind::Sync).has_value());
    expect_decision(dependencies, sync, Decision::Wait);
    EXPECT_FALSE(dependencies.complete(sync, sync.order()));
    ASSERT_TRUE(dependencies.set_unknown(sync, sync.order(), error));
    expect_error(dependencies.decide(sync, 0), EINVAL);
    EXPECT_FALSE(sync.decision_final());
    expect_error(dependencies.decide(sync, sync.order()), error);
    EXPECT_TRUE(sync.decision_final());
    EXPECT_EQ(prior.classification(), IdentityState::Pending);
    ASSERT_TRUE(dependencies.complete(sync, sync.order()));
    ASSERT_TRUE(dependencies.release(sync, sync.order()));
  }
}

TEST(LlvmLibcAioFileDependenciesTest, EveryQualifiedTypeUsesFullTuple) {
  const FileType types[] = {FileType::Regular, FileType::Directory,
                            FileType::Block,   FileType::Character,
                            FileType::Fifo,    FileType::Socket,
                            FileType::Symlink};
  for (FileType prior_type : types) {
    for (FileType sync_type : types) {
      Dependencies dependencies(2);
      Record prior, sync;
      Identity prior_identity = REGULAR;
      prior_identity.type = prior_type;
      Identity sync_identity = REGULAR;
      sync_identity.type = sync_type;
      admit_known(dependencies, prior, Kind::Operation, prior_identity);
      admit_known(dependencies, sync, Kind::Sync, sync_identity);
      if (prior_type == sync_type) {
        expect_decision(dependencies, sync, Decision::Wait);
        ASSERT_TRUE(dependencies.complete(prior, prior.order()));
      }
      // Includes block-device syncs; this only tests ordering eligibility,
      // not whether the kernel accepts a sync for each of these types.
      expect_decision(dependencies, sync, Decision::Ready);
    }
  }
}

TEST(LlvmLibcAioFileDependenciesTest,
     ZeroIdentityFieldsAreNotUnknownSentinels) {
  Dependencies dependencies(2);
  Record prior, sync;
  Identity zero = {0, 0, 0, FileType::Regular};
  admit_known(dependencies, prior, Kind::Operation, zero);
  admit_known(dependencies, sync, Kind::Sync, zero);
  expect_decision(dependencies, sync, Decision::Wait);
  ASSERT_TRUE(dependencies.complete(prior, prior.order()));
  expect_decision(dependencies, sync, Decision::Ready);
}

TEST(LlvmLibcAioFileDependenciesTest, CapacityReuseAndStaleOrders) {
  Dependencies dependencies(1), zero_capacity(0);
  Record record, excess;
  expect_error(zero_capacity.admit(excess, Kind::Operation), EAGAIN);
  EXPECT_EQ(zero_capacity.last_issued_order(), uint64_t(0));
  admit_known(dependencies, record, Kind::Operation);
  uint64_t old_order = record.order();
  EXPECT_EQ(old_order, uint64_t(1));
  expect_error(dependencies.admit(record, Kind::Operation), EINVAL);
  expect_error(dependencies.admit(excess, Kind::Operation), EAGAIN);
  EXPECT_TRUE(excess.reclaimable());
  EXPECT_EQ(excess.order(), uint64_t(0));
  EXPECT_EQ(dependencies.last_issued_order(), old_order);
  ASSERT_TRUE(dependencies.complete(record, old_order));
  ASSERT_TRUE(dependencies.release(record, old_order));
  EXPECT_TRUE(record.reclaimable());
  EXPECT_EQ(record.order(), uint64_t(0));
  EXPECT_FALSE(dependencies.release(record, old_order));
  ASSERT_TRUE(dependencies.admit(record, Kind::Sync).has_value());
  EXPECT_EQ(record.order(), old_order + 1);
  EXPECT_EQ(record.classification(), IdentityState::Pending);
  EXPECT_FALSE(record.decision_final());
  EXPECT_FALSE(dependencies.set_known(record, old_order, REGULAR));
  EXPECT_FALSE(dependencies.set_unknown(record, old_order, EIO));
  EXPECT_FALSE(dependencies.complete(record, old_order));
  EXPECT_FALSE(dependencies.release(record, old_order));
  expect_error(dependencies.decide(record, old_order), EINVAL);
  EXPECT_FALSE(record.decision_final());
  expect_decision(dependencies, record, Decision::Wait);
  ASSERT_TRUE(dependencies.set_known(record, record.order(), REGULAR));
  expect_decision(dependencies, record, Decision::Ready);
  expect_error(dependencies.decide(record, record.order()), EINVAL);
  ASSERT_TRUE(dependencies.complete(record, record.order()));
  ASSERT_TRUE(dependencies.release(record, record.order()));
}

TEST(LlvmLibcAioFileDependenciesTest, InvalidOwnerOrderAndClassification) {
  Dependencies dependencies(2), foreign(1);
  Record record, sync, other, empty;
  ASSERT_TRUE(dependencies.admit(record, Kind::Operation).has_value());
  ASSERT_TRUE(foreign.admit(other, Kind::Operation).has_value());
  EXPECT_EQ(record.order(), other.order());
  EXPECT_FALSE(foreign.set_known(record, record.order(), REGULAR));
  EXPECT_FALSE(foreign.set_unknown(record, record.order(), EBADF));
  EXPECT_FALSE(foreign.complete(record, record.order()));
  EXPECT_FALSE(foreign.release(record, record.order()));
  expect_error(foreign.admit(record, Kind::Operation), EINVAL);
  expect_error(dependencies.admit(empty, static_cast<Kind>(99)), EINVAL);
  EXPECT_TRUE(empty.reclaimable());
  EXPECT_FALSE(dependencies.set_known(empty, 0, REGULAR));
  EXPECT_FALSE(dependencies.set_unknown(empty, 0, EIO));
  EXPECT_FALSE(dependencies.complete(empty, 0));
  EXPECT_FALSE(dependencies.release(empty, 0));
  EXPECT_FALSE(dependencies.set_known(record, 0, REGULAR));
  EXPECT_FALSE(dependencies.set_unknown(record, record.order(), 0));
  EXPECT_FALSE(dependencies.set_unknown(record, record.order(), -EIO));
  EXPECT_FALSE(dependencies.set_unknown(record, record.order(), EINPROGRESS));
  Identity invalid = REGULAR;
  invalid.type = static_cast<FileType>(99);
  EXPECT_FALSE(dependencies.set_known(record, record.order(), invalid));
  EXPECT_EQ(record.classification(), IdentityState::Pending);
  ASSERT_TRUE(dependencies.set_known(record, record.order(), REGULAR));
  EXPECT_FALSE(dependencies.set_known(record, record.order(), OTHER));
  EXPECT_FALSE(dependencies.set_unknown(record, record.order(), EIO));
  expect_error(dependencies.decide(record, record.order()), EINVAL);
  expect_error(dependencies.decide(empty, 0), EINVAL);
  admit_known(dependencies, sync, Kind::Sync);
  expect_error(foreign.decide(sync, sync.order()), EINVAL);
  expect_error(dependencies.decide(sync, 0), EINVAL);
  expect_error(dependencies.decide(sync, sync.order() + 1), EINVAL);
  EXPECT_FALSE(sync.decision_final());
  expect_decision(dependencies, sync, Decision::Wait);
  ASSERT_TRUE(dependencies.complete(record, record.order()));
  EXPECT_FALSE(dependencies.complete(record, record.order()));
  expect_decision(dependencies, sync, Decision::Ready);
  ASSERT_TRUE(foreign.set_unknown(other, other.order(), EBADF));
  EXPECT_FALSE(foreign.set_known(other, other.order(), REGULAR));
  EXPECT_FALSE(foreign.set_unknown(other, other.order(), EIO));
}

TEST(LlvmLibcAioFileDependenciesTest, OrderExhaustionNeverWraps) {
  Dependencies dependencies(2, UINT64_MAX - 1);
  Record last, blocked;
  admit_known(dependencies, last, Kind::Operation);
  EXPECT_EQ(last.order(), UINT64_MAX);
  EXPECT_EQ(dependencies.last_issued_order(), UINT64_MAX);
  expect_error(dependencies.admit(blocked, Kind::Operation), EOVERFLOW);
  EXPECT_TRUE(blocked.reclaimable());
  ASSERT_TRUE(dependencies.complete(last, last.order()));
  ASSERT_TRUE(dependencies.release(last, last.order()));
  expect_error(dependencies.admit(last, Kind::Operation), EOVERFLOW);
  EXPECT_EQ(dependencies.outstanding(), uint32_t(0));
  EXPECT_EQ(dependencies.last_issued_order(), UINT64_MAX);
  Dependencies restored(1, dependencies.last_issued_order());
  expect_error(restored.admit(blocked, Kind::Sync), EOVERFLOW);
  EXPECT_TRUE(blocked.reclaimable());
}
