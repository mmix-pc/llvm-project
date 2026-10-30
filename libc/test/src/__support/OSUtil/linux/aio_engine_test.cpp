//===-- Tests for the Linux AIO submission engine -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/linux/aio_engine.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::AioFileDependencies;
using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;

namespace {
struct FakeRing {
  io_uring_sqe sq[64]{};
  io_uring_cqe cq[64]{};
  unsigned head = 0, tail = 0, cq_head = 0, cq_tail = 0;
  unsigned next_personality = 1, personalities = 0;
  unsigned submits = 0, submit_limit = 64;
  int submit_error = 0, enqueue_error = 0, unregister_error = 0;

  ErrorOr<unsigned> register_personality() {
    ++personalities;
    return next_personality++;
  }
  ErrorOr<void> unregister_personality(unsigned) {
    if (unregister_error)
      return Error(unregister_error);
    --personalities;
    return {};
  }
  ErrorOr<void> check_integrity() { return {}; }
  ErrorOr<void> enqueue(const io_uring_sqe &entry) {
    if (enqueue_error)
      return Error(enqueue_error);
    if (tail == 64)
      return Error(EAGAIN);
    sq[tail++] = entry;
    return {};
  }
  ErrorOr<unsigned> submit() {
    ++submits;
    unsigned count = tail - head;
    if (count > submit_limit)
      count = submit_limit;
    head += count;
    if (submit_error)
      return Error(submit_error);
    return count;
  }
  ErrorOr<void> withdraw_last(uint64_t token) {
    if (tail == head)
      return Error(EAGAIN);
    if (sq[tail - 1].user_data != token)
      return Error(EINVAL);
    --tail;
    return {};
  }
  ErrorOr<void> poll(io_uring_cqe &result) {
    if (cq_head == cq_tail)
      return Error(EAGAIN);
    result = cq[cq_head++];
    return {};
  }
  void complete(unsigned index, int result) {
    cq[cq_tail++] = {sq[index].user_data, result, 0};
  }
  void identify(unsigned index, uint64_t inode, int result = 0) {
    auto *stat = reinterpret_cast<LIBC_NAMESPACE::internal::kernel_statx_buf *>(
        uintptr_t(sq[index].off));
    stat->stx_ino = inode;
    complete(index, result);
  }
};
struct ResourceState {
  unsigned closed = 0;
  int duplicate_error = 0;
  void (*on_close)(void *) = nullptr;
  void *context = nullptr;
};
struct ResourceOperations {
  ResourceState *state;
  ErrorOr<int> duplicate(int fd) {
    if (state->duplicate_error)
      return Error(state->duplicate_error);
    return fd + 100;
  }
  ErrorOr<int> close(int) {
    ++state->closed;
    if (state->on_close)
      state->on_close(state->context);
    return 0;
  }
};
using Engine = LIBC_NAMESPACE::AioEngine<FakeRing, ResourceOperations>;
using Op = Engine::Operation;
ErrorOr<AioFileDependencies::Identity>
classify(void *context, int,
         const LIBC_NAMESPACE::internal::kernel_statx_buf &s) {
  if (context)
    return Error(ENOTSUP);
  return AioFileDependencies::Identity{1, 2, s.stx_ino,
                                       AioFileDependencies::FileType::Regular};
}
Engine::Input input(const void *address, Op op = Op::Read,
                    bool notify = false) {
  return {address, 7, op, nullptr, 8, 42, notify};
}
struct Fixture {
  FakeRing ring;
  ResourceState resources;
  uint64_t watermark = 0;
  Engine engine;
  Fixture(void *classifier_context = nullptr)
      : engine(ring, watermark, 17, 16, classify, classifier_context) {}
  Engine::Request make_request() { return Engine::Request(ring, {&resources}); }
};
} // namespace

TEST(LlvmLibcAioEngineTest, AdmissionDoesNotSubmitAndUsesTrustedBinding) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(8)));
  auto r = f.make_request();
  int cb = 0;
  ASSERT_TRUE(bool(r.resources.capture(7)));
  ASSERT_TRUE(bool(f.engine.admit(r, input(&cb))));
  EXPECT_EQ(f.ring.submits, 0U);
  EXPECT_EQ(f.ring.tail, 0U);
  EXPECT_EQ(*f.engine.error(&cb), EINPROGRESS);
  EXPECT_EQ(f.engine.error(reinterpret_cast<void *>(1)).error(), EINVAL);
  EXPECT_EQ(f.engine.take_result(&cb).error(), EINPROGRESS);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_EQ(f.ring.tail, 2U);
  EXPECT_EQ(f.ring.sq[0].opcode, static_cast<unsigned char>(IORING_OP_STATX));
  EXPECT_EQ(f.ring.sq[1].opcode, static_cast<unsigned char>(IORING_OP_READ));
  EXPECT_EQ(f.ring.sq[1].fd, 107);
  EXPECT_EQ(f.ring.sq[1].personality, static_cast<unsigned short>(1));
  EXPECT_EQ(uint64_t(f.ring.sq[1].off), uint64_t(42));
  EXPECT_EQ(f.ring.sq[1].flags, static_cast<unsigned char>(IOSQE_ASYNC));
  f.ring.complete(1, 5);
  ASSERT_EQ(f.engine.progress(), 0);
  auto result = f.engine.take_result(&cb);
  ASSERT_TRUE(bool(result));
  EXPECT_EQ(result->value, ssize_t(5));
  EXPECT_EQ(result->error, 0);
  EXPECT_EQ(f.engine.error(&cb).error(), EINVAL);
  EXPECT_FALSE(bool(f.engine.retire(r))); // Metadata still owns the descriptor.
  f.ring.identify(0, 10);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(bool(f.engine.retire(r)));
  EXPECT_EQ(f.resources.closed, 1U);
  EXPECT_EQ(f.ring.personalities, 0U);
}

TEST(LlvmLibcAioEngineTest, DuplicateAddressAndReuseAfterResult) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(8)));
  auto old = f.make_request();
  auto fresh = f.make_request();
  int cb = 0;
  ASSERT_TRUE(bool(old.resources.capture(7)));
  ASSERT_TRUE(bool(fresh.resources.capture(8)));
  ASSERT_TRUE(bool(f.engine.admit(old, input(&cb, Op::Write, true))));
  EXPECT_EQ(f.engine.admit(fresh, input(&cb)).error(), EBUSY);
  ASSERT_TRUE(bool(f.engine.cancel_queued(old)));
  auto result = f.engine.take_result(&cb);
  ASSERT_TRUE(bool(result));
  EXPECT_EQ(result->error, ECANCELED);
  ASSERT_TRUE(bool(f.engine.admit(fresh, input(&cb))));
  EXPECT_GT(fresh.state().identity().token, old.state().identity().token);
  EXPECT_EQ(f.engine.find(&cb), &fresh);
  EXPECT_FALSE(bool(f.engine.retire(old)));
  ASSERT_TRUE(bool(old.state().take_notification()));
  EXPECT_FALSE(bool(f.engine.retire(old)));
  ASSERT_TRUE(bool(old.state().release_notification()));
  ASSERT_TRUE(bool(f.engine.retire(old)));
  ASSERT_TRUE(bool(f.engine.cancel_queued(fresh)));
  ASSERT_TRUE(bool(f.engine.take_result(&cb)));
  ASSERT_TRUE(bool(f.engine.retire(fresh)));
}

TEST(LlvmLibcAioEngineTest, QueryDistinguishesOperationEinvalFromBadBinding) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(8)));
  auto r = f.make_request();
  int cb = 0;
  ASSERT_TRUE(bool(r.resources.capture(7)));
  ASSERT_TRUE(bool(f.engine.admit(r, input(&cb))));
  ASSERT_EQ(f.engine.progress(), 0);
  f.ring.identify(0, 1);
  f.ring.complete(1, -EINVAL);
  ASSERT_EQ(f.engine.progress(), 0);
  auto status = f.engine.error(&cb);
  ASSERT_TRUE(bool(status));
  EXPECT_EQ(*status, EINVAL);
  auto result = f.engine.take_result(&cb);
  ASSERT_TRUE(bool(result));
  EXPECT_EQ(result->value, ssize_t(-1));
  EXPECT_EQ(result->error, EINVAL);
  EXPECT_FALSE(bool(f.engine.error(&cb)));
  ASSERT_TRUE(bool(f.engine.retire(r)));
}

TEST(LlvmLibcAioEngineTest, ShortSubmissionDoesNotRepublish) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(8)));
  auto r = f.make_request();
  int cb = 0;
  ASSERT_TRUE(bool(r.resources.capture(7)));
  ASSERT_TRUE(bool(f.engine.admit(r, input(&cb))));
  f.ring.submit_limit = 1;
  ASSERT_EQ(f.engine.progress(), 0);
  EXPECT_EQ(f.ring.head, 1U);
  EXPECT_EQ(f.ring.tail, 2U);
  ASSERT_EQ(f.engine.progress(), 0);
  EXPECT_EQ(f.ring.head, 2U);
  EXPECT_EQ(f.ring.tail, 2U);
  EXPECT_FALSE(bool(f.engine.cancel_queued(r)));
  f.ring.identify(0, 1);
  f.ring.complete(1, 8);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(bool(f.engine.take_result(&cb)));
  ASSERT_TRUE(bool(f.engine.retire(r)));
}

TEST(LlvmLibcAioEngineTest, EnterFailureWithdrawsOnlyUnconsumedTail) {
  for (unsigned consumed = 0; consumed != 3; ++consumed) {
    Fixture f;
    ASSERT_TRUE(bool(f.engine.initialize(8)));
    auto r = f.make_request();
    int cb = 0;
    ASSERT_TRUE(bool(r.resources.capture(7)));
    ASSERT_TRUE(bool(f.engine.admit(r, input(&cb))));
    f.ring.submit_limit = consumed;
    f.ring.submit_error = EINTR;
    ASSERT_EQ(f.engine.progress(), EINTR);
    EXPECT_EQ(f.ring.tail, consumed);
    EXPECT_EQ(r.complete(), consumed < 2);
    f.ring.submit_error = 0;
    if (consumed)
      f.ring.identify(0, 1);
    if (consumed == 2)
      f.ring.complete(1, 8);
    ASSERT_EQ(f.engine.progress(), 0);
    auto result = f.engine.take_result(&cb);
    ASSERT_TRUE(bool(result));
    EXPECT_EQ(result->error, consumed < 2 ? EINTR : 0);
    ASSERT_TRUE(bool(f.engine.retire(r)));
  }
}

TEST(LlvmLibcAioEngineTest, SameFileSyncWaitsButUnrelatedTransferDoesNot) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(12)));
  auto same = f.make_request();
  auto other = f.make_request();
  auto sync = f.make_request();
  int a = 0, b = 0, c = 0;
  ASSERT_TRUE(bool(same.resources.capture(7)));
  ASSERT_TRUE(bool(other.resources.capture(8)));
  ASSERT_TRUE(bool(sync.resources.capture(9)));
  ASSERT_TRUE(bool(f.engine.admit(same, input(&a, Op::Write))));
  ASSERT_TRUE(bool(f.engine.admit(other, input(&b))));
  ASSERT_TRUE(bool(f.engine.admit(sync, input(&c, Op::DataSync))));
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_EQ(f.ring.tail, 5U);
  f.ring.identify(0, 1);
  f.ring.identify(2, 2);
  f.ring.identify(4, 1);
  ASSERT_EQ(f.engine.progress(), 0);
  EXPECT_FALSE(bool(sync.published()));
  f.ring.complete(1, 8);
  ASSERT_EQ(f.engine.progress(), 0);
  EXPECT_TRUE(bool(sync.published()));
  EXPECT_FALSE(bool(other.complete()));
  ASSERT_EQ(f.ring.tail, 6U);
  EXPECT_EQ(f.ring.sq[5].opcode, static_cast<unsigned char>(IORING_OP_FSYNC));
  EXPECT_EQ(f.ring.sq[5].fsync_flags, unsigned(IORING_FSYNC_DATASYNC));
  f.ring.complete(5, 0);
  f.ring.complete(3, 8);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(bool(f.engine.take_result(&a)));
  ASSERT_TRUE(bool(f.engine.take_result(&b)));
  ASSERT_TRUE(bool(f.engine.take_result(&c)));
  ASSERT_TRUE(bool(f.engine.retire(same)));
  ASSERT_TRUE(bool(f.engine.retire(other)));
  ASSERT_TRUE(bool(f.engine.retire(sync)));
}

TEST(LlvmLibcAioEngineTest, FailedPredecessorSurvivesReturnUntilSyncDecision) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(8)));
  auto first = f.make_request();
  auto sync = f.make_request();
  int a = 0, b = 0;
  ASSERT_TRUE(bool(first.resources.capture(7)));
  ASSERT_TRUE(bool(sync.resources.capture(8)));
  ASSERT_TRUE(bool(f.engine.admit(first, input(&a, Op::Write))));
  ASSERT_TRUE(bool(f.engine.admit(sync, input(&b, Op::Sync))));
  ASSERT_EQ(f.engine.progress(), 0);
  f.ring.complete(1, -EIO);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(bool(f.engine.take_result(&a)));
  EXPECT_FALSE(bool(f.engine.retire(first)));
  f.ring.identify(0, 1);
  f.ring.identify(2, 1);
  ASSERT_EQ(f.engine.progress(), 0);
  EXPECT_FALSE(bool(sync.published()));
  EXPECT_EQ(*f.engine.error(&b), EIO);
  ASSERT_TRUE(bool(f.engine.take_result(&b)));
  ASSERT_TRUE(bool(f.engine.retire(first)));
  ASSERT_TRUE(bool(f.engine.retire(sync)));
}

TEST(LlvmLibcAioEngineTest, UnknownIdentityFailsSyncNotTransfer) {
  Fixture f(reinterpret_cast<void *>(1));
  ASSERT_TRUE(bool(f.engine.initialize(8)));
  auto first = f.make_request();
  auto sync = f.make_request();
  int a = 0, b = 0;
  ASSERT_TRUE(bool(first.resources.capture(7)));
  ASSERT_TRUE(bool(sync.resources.capture(8)));
  ASSERT_TRUE(bool(f.engine.admit(first, input(&a))));
  ASSERT_TRUE(bool(f.engine.admit(sync, input(&b, Op::Sync))));
  ASSERT_EQ(f.engine.progress(), 0);
  f.ring.identify(0, 1);
  f.ring.identify(2, 1);
  ASSERT_EQ(f.engine.progress(), 0);
  EXPECT_EQ(*f.engine.error(&b), ENOTSUP);
  EXPECT_EQ(*f.engine.error(&a), EINPROGRESS);
  EXPECT_FALSE(bool(sync.published()));
  f.ring.complete(1, 8);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(bool(f.engine.take_result(&a)));
  ASSERT_TRUE(bool(f.engine.take_result(&b)));
  ASSERT_TRUE(bool(f.engine.retire(first)));
  ASSERT_TRUE(bool(f.engine.retire(sync)));
}

TEST(LlvmLibcAioEngineTest, QueuedSyncCancelStillDrainsPublishedMetadata) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(8)));
  auto r = f.make_request();
  int cb = 0;
  ASSERT_TRUE(bool(r.resources.capture(7)));
  ASSERT_TRUE(bool(f.engine.admit(r, input(&cb, Op::Sync))));
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_EQ(f.ring.tail, 1U);
  ASSERT_TRUE(bool(f.engine.cancel_queued(r)));
  EXPECT_EQ(*f.engine.error(&cb), ECANCELED);
  ASSERT_TRUE(bool(f.engine.take_result(&cb)));
  EXPECT_FALSE(bool(f.engine.retire(r)));
  f.ring.identify(0, 1);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(bool(f.engine.retire(r)));
}

TEST(LlvmLibcAioEngineTest, AdmissionReservesControlHeadroom) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(3)));
  auto first = f.make_request();
  auto rejected = f.make_request();
  int a = 0, b = 0;
  ASSERT_TRUE(bool(first.resources.capture(7)));
  ASSERT_TRUE(bool(rejected.resources.capture(8)));
  ASSERT_TRUE(bool(f.engine.admit(first, input(&a))));
  EXPECT_EQ(f.engine.admit(rejected, input(&b)).error(), EAGAIN);
  EXPECT_EQ(f.engine.find(&b), static_cast<Engine::Request *>(nullptr));
  ASSERT_TRUE(bool(rejected.resources.release()));
  ASSERT_TRUE(bool(f.engine.cancel_queued(first)));
  ASSERT_TRUE(bool(f.engine.take_result(&a)));
  ASSERT_TRUE(bool(f.engine.retire(first)));
}

TEST(LlvmLibcAioEngineTest, CleanupFailureRetainsOnlyPersonality) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(3)));
  auto r = f.make_request();
  int cb = 0;
  ASSERT_TRUE(bool(r.resources.capture(7)));
  ASSERT_TRUE(bool(f.engine.admit(r, input(&cb))));
  ASSERT_TRUE(bool(f.engine.cancel_queued(r)));
  ASSERT_TRUE(bool(f.engine.take_result(&cb)));
  f.ring.unregister_error = EBUSY;
  EXPECT_FALSE(bool(f.engine.retire(r)));
  EXPECT_EQ(f.resources.closed, 1U);
  EXPECT_FALSE(bool(f.engine.retire(r)));
  EXPECT_EQ(f.resources.closed, 1U);
  f.ring.unregister_error = 0;
  ASSERT_TRUE(bool(f.engine.retire(r)));
  EXPECT_EQ(f.resources.closed, 1U);
  EXPECT_TRUE(bool(f.engine.empty()));
}

TEST(LlvmLibcAioEngineTest, EnqueueFailureProducesExplicitErrorsWithoutCqe) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(3)));
  auto r = f.make_request();
  int cb = 0;
  ASSERT_TRUE(bool(r.resources.capture(7)));
  ASSERT_TRUE(bool(f.engine.admit(r, input(&cb))));
  f.ring.enqueue_error = ENOMEM;
  ASSERT_EQ(f.engine.progress(), 0);
  EXPECT_EQ(f.ring.tail, 0U);
  EXPECT_EQ(*f.engine.error(&cb), ENOMEM);
  ASSERT_TRUE(bool(f.engine.take_result(&cb)));
  ASSERT_TRUE(bool(f.engine.retire(r)));
}

TEST(LlvmLibcAioEngineTest, QueriesRemainAvailableBetweenStageAndEnterReturn) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(8)));
  auto r = f.make_request();
  auto queued = f.make_request();
  int a = 0, b = 0;
  ASSERT_TRUE(bool(r.resources.capture(7)));
  ASSERT_TRUE(bool(queued.resources.capture(8)));
  ASSERT_TRUE(bool(f.engine.admit(r, input(&a))));
  ASSERT_EQ(f.engine.stage(), 0);
  EXPECT_TRUE(f.engine.submit_in_progress());
  EXPECT_EQ(f.engine.stage(), EBUSY);
  EXPECT_EQ(*f.engine.error(&a), EINPROGRESS);
  ASSERT_TRUE(bool(f.engine.admit(queued, input(&b))));
  ASSERT_TRUE(f.engine.cancel_queued(queued));
  ASSERT_TRUE(bool(f.engine.take_result(&b)));
  EXPECT_FALSE(f.engine.retire(queued));
  ASSERT_EQ(f.engine.finish_submit(f.ring.submit()), 0);
  EXPECT_FALSE(f.engine.submit_in_progress());
  ASSERT_TRUE(f.engine.retire(queued));
  f.ring.identify(0, 1);
  f.ring.complete(1, 8);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(bool(f.engine.take_result(&a)));
  ASSERT_TRUE(f.engine.retire(r));
}

TEST(LlvmLibcAioEngineTest, CancelAndOriginalCompletionsKeepSeparateOwners) {
  const bool orders[] = {false, true};
  for (bool cancel_first : orders) {
    Fixture f;
    ASSERT_TRUE(bool(f.engine.initialize(3)));
    auto r = f.make_request();
    Engine::Cancel cancel;
    int cb = 0;
    ASSERT_TRUE(bool(r.resources.capture(7)));
    ASSERT_TRUE(bool(f.engine.admit(r, input(&cb))));
    ASSERT_EQ(f.engine.progress(), 0);
    ASSERT_TRUE(bool(f.engine.cancel_published(r, cancel)));
    ASSERT_EQ(f.engine.progress(), 0);
    ASSERT_EQ(f.ring.tail, 3U);
    EXPECT_EQ(f.ring.sq[2].opcode,
              static_cast<unsigned char>(IORING_OP_ASYNC_CANCEL));
    EXPECT_EQ(uint64_t(f.ring.sq[2].addr), r.state().identity().token);
    f.ring.identify(0, 1);
    f.ring.complete(cancel_first ? 2 : 1, cancel_first ? 0 : -ECANCELED);
    ASSERT_EQ(f.engine.progress(), 0);
    EXPECT_EQ(r.complete(), !cancel_first);
    EXPECT_EQ(cancel.complete(), cancel_first);
    if (cancel_first) {
      EXPECT_EQ(f.engine.original_result(cancel).error(), EINPROGRESS);
      EXPECT_EQ(*f.engine.cancel_result(cancel), 0);
    } else {
      ASSERT_TRUE(bool(f.engine.take_result(&cb)));
      EXPECT_FALSE(f.engine.retire(r));
    }
    f.ring.complete(cancel_first ? 1 : 2, cancel_first ? -ECANCELED : -ENOENT);
    ASSERT_EQ(f.engine.progress(), 0);
    auto original = f.engine.original_result(cancel);
    ASSERT_TRUE(bool(original));
    EXPECT_EQ(original->error, ECANCELED);
    if (cancel_first)
      ASSERT_TRUE(bool(f.engine.take_result(&cb)));
    EXPECT_FALSE(f.engine.retire(r));
    ASSERT_TRUE(f.engine.retire_cancel(cancel));
    ASSERT_TRUE(f.engine.retire(r));
  }
}

TEST(LlvmLibcAioEngineTest, CancelWithdrawalDoesNotCancelOriginal) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(3)));
  auto r = f.make_request();
  Engine::Cancel cancel;
  int cb = 0;
  ASSERT_TRUE(bool(r.resources.capture(7)));
  ASSERT_TRUE(bool(f.engine.admit(r, input(&cb))));
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(bool(f.engine.cancel_published(r, cancel)));
  f.ring.submit_limit = 0;
  f.ring.submit_error = EIO;
  ASSERT_EQ(f.engine.progress(), EIO);
  EXPECT_TRUE(cancel.complete());
  EXPECT_FALSE(r.complete());
  EXPECT_EQ(f.engine.cancel_result(cancel).error(), EIO);
  EXPECT_EQ(f.engine.original_result(cancel).error(), EINPROGRESS);
  f.ring.submit_error = 0;
  f.ring.identify(0, 1);
  f.ring.complete(1, 8);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(bool(f.engine.take_result(&cb)));
  EXPECT_FALSE(f.engine.retire(r));
  ASSERT_TRUE(f.engine.retire_cancel(cancel));
  ASSERT_TRUE(f.engine.retire(r));
}

TEST(LlvmLibcAioEngineTest, CancelEnqueueFailureReleasesOnlyPreparedControl) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(3)));
  auto r = f.make_request();
  Engine::Cancel cancel;
  int cb = 0;
  ASSERT_TRUE(bool(r.resources.capture(7)));
  ASSERT_TRUE(bool(f.engine.admit(r, input(&cb))));
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(bool(f.engine.cancel_published(r, cancel)));
  f.ring.enqueue_error = ENOMEM;
  ASSERT_EQ(f.engine.progress(), 0);
  EXPECT_EQ(f.engine.cancel_result(cancel).error(), ENOMEM);
  EXPECT_EQ(*f.engine.error(&cb), EINPROGRESS);
  f.ring.identify(0, 1);
  f.ring.complete(1, 8);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(bool(f.engine.take_result(&cb)));
  ASSERT_TRUE(f.engine.retire(r));
  EXPECT_FALSE(f.engine.empty());
  ASSERT_TRUE(f.engine.retire_cancel(cancel));
  EXPECT_TRUE(f.engine.empty());
}

TEST(LlvmLibcAioEngineTest, AbortedControlDoesNotDereferenceDestroyedRequest) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(3)));
  alignas(Engine::Request) unsigned char storage[sizeof(Engine::Request)];
  auto *r = new (storage) Engine::Request(f.ring, {&f.resources});
  Engine::Cancel cancel;
  int cb = 0;
  ASSERT_TRUE(bool(r->resources.capture(7)));
  ASSERT_TRUE(bool(f.engine.admit(*r, input(&cb))));
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(bool(f.engine.cancel_published(*r, cancel)));
  f.ring.enqueue_error = ENOMEM;
  ASSERT_EQ(f.engine.progress(), 0);
  f.ring.identify(0, 1);
  f.ring.complete(1, 8);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(bool(f.engine.take_result(&cb)));
  ASSERT_TRUE(f.engine.retire(*r));
  r->~Request();
  for (auto &byte : storage)
    byte = 0xA5;
  EXPECT_EQ(f.engine.cancel_result(cancel).error(), ENOMEM);
  EXPECT_EQ(f.engine.original_result(cancel).error(), EINVAL);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(f.engine.retire_cancel(cancel));
  EXPECT_TRUE(f.engine.empty());
}

TEST(LlvmLibcAioEngineTest, OriginalCompletionOvertakesPreparedCancellation) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(3)));
  auto r = f.make_request();
  Engine::Cancel cancel;
  int cb = 0;
  ASSERT_TRUE(bool(r.resources.capture(7)));
  ASSERT_TRUE(bool(f.engine.admit(r, input(&cb))));
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(bool(f.engine.cancel_published(r, cancel)));
  f.ring.identify(0, 1);
  f.ring.complete(1, 8);
  ASSERT_EQ(f.engine.progress(), 0);
  EXPECT_EQ(f.ring.tail, 2U);
  EXPECT_TRUE(cancel.complete());
  EXPECT_EQ(f.engine.cancel_result(cancel).error(), EALREADY);
  auto result = f.engine.take_result(&cb);
  ASSERT_TRUE(bool(result));
  EXPECT_EQ(result->value, ssize_t(8));
  ASSERT_TRUE(f.engine.retire(r));
  ASSERT_TRUE(f.engine.retire_cancel(cancel));
}

TEST(LlvmLibcAioEngineTest,
     MetadataTokenExhaustionRollsBackOriginalReservation) {
  FakeRing ring;
  ResourceState resources;
  uint64_t watermark = UINT64_MAX - 1;
  Engine engine(ring, watermark, 17, 16, classify);
  ASSERT_TRUE(bool(engine.initialize(3)));
  Engine::Request r(ring, {&resources});
  int cb = 0;
  ASSERT_TRUE(bool(r.resources.capture(7)));
  EXPECT_EQ(engine.admit(r, input(&cb)).error(), EOVERFLOW);
  EXPECT_EQ(watermark, UINT64_MAX);
  EXPECT_TRUE(engine.empty());
  EXPECT_EQ(engine.find(&cb), static_cast<Engine::Request *>(nullptr));
  EXPECT_EQ(ring.tail, 0U);
  EXPECT_EQ(engine.admit(r, input(&cb)).error(), EOVERFLOW);
  ASSERT_TRUE(bool(r.resources.release()));
  EXPECT_EQ(resources.closed, 1U);
}

TEST(LlvmLibcAioEngineTest, SplitCleanupAllowsQueriesAndAddressReuse) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(8)));
  auto old = f.make_request();
  auto fresh = f.make_request();
  int cb = 0;
  ASSERT_TRUE(bool(old.resources.capture(7)));
  ASSERT_TRUE(bool(fresh.resources.capture(8)));
  ASSERT_TRUE(bool(f.engine.admit(old, input(&cb))));
  ASSERT_TRUE(f.engine.cancel_queued(old));
  EXPECT_FALSE(f.engine.begin_retire(old)); // Result still belongs to caller.
  ASSERT_TRUE(bool(f.engine.take_result(&cb)));
  ASSERT_TRUE(f.engine.begin_retire(old));
  EXPECT_TRUE(f.engine.cleanup_in_progress());
  EXPECT_EQ(f.resources.closed, 0U);
  EXPECT_EQ(f.ring.personalities, 2U);
  EXPECT_FALSE(f.engine.begin_retire(old));
  EXPECT_EQ(f.engine.stage(), EBUSY);
  EXPECT_FALSE(f.engine.empty());
  ASSERT_TRUE(bool(f.engine.admit(fresh, input(&cb))));

  struct Observer {
    Engine *engine;
    const void *cb;
    unsigned calls = 0;
    static void check(void *context) {
      auto &self = *static_cast<Observer *>(context);
      ++self.calls;
      EXPECT_TRUE(self.engine->cleanup_in_progress());
      EXPECT_EQ(*self.engine->error(self.cb), EINPROGRESS);
      EXPECT_EQ(self.engine->take_result(self.cb).error(), EINPROGRESS);
      EXPECT_EQ(self.engine->stage(), EBUSY);
    }
  } observer{&f.engine, &cb};
  f.resources.on_close = Observer::check;
  f.resources.context = &observer;
  ASSERT_TRUE(bool(old.resources.release()));
  EXPECT_EQ(observer.calls, 1U);
  f.resources.on_close = nullptr;
  ASSERT_TRUE(f.engine.finish_retire(old));
  EXPECT_FALSE(f.engine.cleanup_in_progress());
  EXPECT_EQ(f.engine.find(&cb), &fresh);
  EXPECT_EQ(f.resources.closed, 1U);
  EXPECT_EQ(f.ring.personalities, 1U);
  ASSERT_TRUE(f.engine.cancel_queued(fresh));
  ASSERT_TRUE(bool(f.engine.take_result(&cb)));
  ASSERT_TRUE(f.engine.retire(fresh));
  EXPECT_TRUE(f.engine.empty());
}

TEST(LlvmLibcAioEngineTest, SplitCleanupFailureRetainsOwnership) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(8)));
  auto first = f.make_request();
  auto second = f.make_request();
  int a = 0, b = 0;
  ASSERT_TRUE(bool(first.resources.capture(7)));
  ASSERT_TRUE(bool(second.resources.capture(8)));
  ASSERT_TRUE(bool(f.engine.admit(first, input(&a))));
  ASSERT_TRUE(bool(f.engine.admit(second, input(&b))));
  ASSERT_TRUE(f.engine.cancel_queued(first));
  ASSERT_TRUE(f.engine.cancel_queued(second));
  ASSERT_TRUE(bool(f.engine.take_result(&a)));
  ASSERT_TRUE(bool(f.engine.take_result(&b)));
  ASSERT_TRUE(f.engine.begin_retire(first));
  EXPECT_FALSE(f.engine.begin_retire(second));
  f.ring.unregister_error = EBUSY;
  EXPECT_EQ(first.resources.release().error(), EBUSY);
  EXPECT_FALSE(f.engine.finish_retire(first));
  EXPECT_FALSE(f.engine.cleanup_in_progress());
  EXPECT_EQ(f.resources.closed, 1U);
  EXPECT_EQ(f.ring.personalities, 2U);
  EXPECT_FALSE(f.engine.empty());
  f.ring.unregister_error = 0;
  ASSERT_TRUE(f.engine.begin_retire(first));
  ASSERT_TRUE(bool(first.resources.release()));
  ASSERT_TRUE(f.engine.finish_retire(first));
  EXPECT_EQ(f.resources.closed, 1U);
  ASSERT_TRUE(f.engine.retire(second));
  EXPECT_EQ(f.resources.closed, 2U);
  EXPECT_EQ(f.ring.personalities, 0U);
  EXPECT_TRUE(f.engine.empty());
}
