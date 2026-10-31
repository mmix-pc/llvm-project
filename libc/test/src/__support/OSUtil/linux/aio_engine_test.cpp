//===-- Tests for the Linux AIO submission engine -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/func/aligned_alloc.h"
#include "src/__support/OSUtil/linux/aio_engine.h"
#include "src/__support/threads/aio_storage.h"
#include "src/__support/threads/aio_suspend_observers.h"
#include "test/UnitTest/Test.h"

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
};
struct ResourceState {
  unsigned closed = 0;
  int next_descriptor = 100;
  int duplicate_error = 0;
  void (*on_close)(void *) = nullptr;
  void *context = nullptr;
};
struct ResourceOperations {
  ResourceState *state;
  ErrorOr<int> duplicate(int) {
    if (state->duplicate_error)
      return Error(state->duplicate_error);
    return ++state->next_descriptor;
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
Engine::Input input(const void *address, Op op = Op::Read, bool notify = false,
                    int fd = 7) {
  return {address, fd, op, nullptr, 8, 42, notify};
}
struct Fixture {
  FakeRing ring;
  ResourceState resources;
  uint64_t watermark = 0;
  Engine engine;
  Fixture() : engine(ring, watermark, 17, 16) {}
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
  EXPECT_EQ(f.watermark, uint64_t(1));
  EXPECT_EQ(*f.engine.error(&cb), EINPROGRESS);
  EXPECT_EQ(f.engine.error(reinterpret_cast<void *>(1)).error(), EINVAL);
  EXPECT_EQ(f.engine.take_result(&cb).error(), EINPROGRESS);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_EQ(f.ring.tail, 1U);
  EXPECT_EQ(f.ring.sq[0].opcode, static_cast<unsigned char>(IORING_OP_READ));
  EXPECT_EQ(f.ring.sq[0].fd, 101);
  EXPECT_EQ(f.ring.sq[0].personality, static_cast<unsigned short>(1));
  EXPECT_EQ(uint64_t(f.ring.sq[0].off), uint64_t(42));
  EXPECT_EQ(f.ring.sq[0].flags, static_cast<unsigned char>(IOSQE_ASYNC));
  f.ring.complete(0, 5);
  ASSERT_EQ(f.engine.progress(), 0);
  auto result = f.engine.take_result(&cb);
  ASSERT_TRUE(bool(result));
  EXPECT_EQ(result->value, ssize_t(5));
  EXPECT_EQ(result->error, 0);
  EXPECT_EQ(f.engine.error(&cb).error(), EINVAL);
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
  f.ring.complete(0, -EINVAL);
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

TEST(LlvmLibcAioEngineTest, SameFdOperationPairsWaitOnlyForOriginalTerminal) {
  const Op ops[] = {Op::Read, Op::Write, Op::Sync, Op::DataSync};
  const int errors[] = {0, EIO};
  for (Op first_op : ops) {
    for (Op second_op : ops) {
      for (int error : errors) {
        Fixture f;
        ASSERT_TRUE(bool(f.engine.initialize(4)));
        auto first = f.make_request();
        auto second = f.make_request();
        auto other = f.make_request();
        int a = 0, b = 0, c = 0;
        ASSERT_TRUE(bool(first.resources.capture(7)));
        ASSERT_TRUE(bool(second.resources.capture(7)));
        ASSERT_TRUE(bool(other.resources.capture(8)));
        ASSERT_TRUE(bool(f.engine.admit(first, input(&a, first_op, true))));
        ASSERT_TRUE(bool(f.engine.admit(second, input(&b, second_op))));
        ASSERT_TRUE(
            bool(f.engine.admit(other, input(&c, Op::Write, false, 8))));
        ASSERT_EQ(f.engine.progress(), 0);
        ASSERT_EQ(f.ring.tail, 2U);
        EXPECT_EQ(uint64_t(f.ring.sq[0].user_data),
                  first.state().identity().token);
        EXPECT_EQ(uint64_t(f.ring.sq[1].user_data),
                  other.state().identity().token);
        EXPECT_FALSE(second.published());
        // An unrelated fd completes while the first original remains blocked,
        // including a read followed by a write on a bidirectional fd.
        f.ring.complete(1, 8);
        ASSERT_EQ(f.engine.progress(), 0);
        EXPECT_EQ(*f.engine.error(&c), 0);
        EXPECT_EQ(*f.engine.error(&a), EINPROGRESS);
        EXPECT_EQ(*f.engine.error(&b), EINPROGRESS);
        EXPECT_FALSE(second.published());
        ASSERT_EQ(f.ring.tail, 2U);
        const bool first_sync =
            first_op == Op::Sync || first_op == Op::DataSync;
        f.ring.complete(0, error ? -error : (first_sync ? 0 : 8));
        ASSERT_EQ(f.engine.progress(), 0);
        EXPECT_TRUE(first.complete());
        EXPECT_EQ(f.engine.find(&a), &first);
        EXPECT_FALSE(f.engine.retire(first));
        const bool second_sync =
            second_op == Op::Sync || second_op == Op::DataSync;
        if (error && second_sync) {
          EXPECT_FALSE(second.published());
          EXPECT_TRUE(second.complete());
          EXPECT_EQ(*f.engine.error(&b), error);
          EXPECT_EQ(f.ring.tail, 2U);
        } else {
          // Neither the unconsumed result nor undelivered notification blocks
          // the successor; ordinary I/O never inherits the predecessor error.
          ASSERT_EQ(f.ring.tail, 3U);
          EXPECT_TRUE(second.published());
          EXPECT_EQ(*f.engine.error(&b), EINPROGRESS);
          EXPECT_EQ(uint64_t(f.ring.sq[2].user_data),
                    second.state().identity().token);
          EXPECT_NE(f.ring.sq[0].fd, f.ring.sq[2].fd);
          f.ring.complete(2, second_sync ? 0 : 8);
          ASSERT_EQ(f.engine.progress(), 0);
          EXPECT_EQ(*f.engine.error(&b), 0);
        }
        auto result = f.engine.take_result(&a);
        ASSERT_TRUE(bool(result));
        EXPECT_EQ(result->error, error);
        ASSERT_TRUE(bool(f.engine.take_result(&b)));
        ASSERT_TRUE(bool(f.engine.take_result(&c)));
        ASSERT_TRUE(bool(first.state().take_notification()));
        EXPECT_FALSE(f.engine.retire(first));
        ASSERT_TRUE(first.state().release_notification());
        ASSERT_TRUE(f.engine.retire(first));
        ASSERT_TRUE(f.engine.retire(second));
        ASSERT_TRUE(f.engine.retire(other));
        EXPECT_TRUE(f.engine.empty());
      }
    }
  }
}

TEST(LlvmLibcAioEngineTest, AppendAndStreamWritesExecuteInAdmissionOrder) {
  // Inject an append/stream sink: each original writes its bytes when executed,
  // ignoring the explicit position for append, or using the stream sentinel.
  // Descriptor flag/offset normalization belongs to the submitting service.
  const int64_t offsets[] = {42, -1};
  for (int64_t offset : offsets) {
    Fixture f;
    ASSERT_TRUE(bool(f.engine.initialize(4)));
    auto first = f.make_request();
    auto second = f.make_request();
    auto third = f.make_request();
    Engine::Request *requests[] = {&first, &second, &third};
    int cb[3]{};
    char payload[3][2] = {{'a', 'b'}, {'c', 'd'}, {'e', 'f'}};
    char output[6]{};
    unsigned written = 0;
    for (unsigned i = 0; i != 3; ++i) {
      ASSERT_TRUE(bool(requests[i]->resources.capture(7)));
      auto in = input(&cb[i], Op::Write, i == 0);
      in.buffer = payload[i];
      in.size = 2;
      in.offset = offset;
      ASSERT_TRUE(bool(f.engine.admit(*requests[i], in)));
    }
    for (unsigned i = 0; i != 3; ++i) {
      ASSERT_EQ(f.engine.progress(), 0);
      ASSERT_EQ(f.ring.tail, i + 1);
      const auto &sqe = f.ring.sq[i];
      EXPECT_EQ(uint64_t(sqe.user_data), requests[i]->state().identity().token);
      EXPECT_EQ(sqe.opcode, static_cast<unsigned char>(IORING_OP_WRITE));
      EXPECT_EQ(uint64_t(sqe.off), uint64_t(offset));
      ASSERT_EQ(sqe.len, 2U);
      auto *data = reinterpret_cast<const char *>(uintptr_t(sqe.addr));
      for (unsigned j = 0; j != sqe.len; ++j)
        output[written++] = data[j];
      f.ring.complete(i, 2);
      if (i == 1) {
        // Keep the first callback owner active while the third is published.
        ASSERT_TRUE(bool(first.state().take_notification()));
      }
    }
    ASSERT_EQ(f.engine.progress(), 0);
    EXPECT_EQ(written, 6U);
    for (unsigned i = 0; i != 6; ++i)
      EXPECT_EQ(output[i], "abcdef"[i]);
    ASSERT_TRUE(first.state().release_notification());
    for (unsigned i = 0; i != 3; ++i) {
      auto result = f.engine.take_result(&cb[i]);
      ASSERT_TRUE(bool(result));
      EXPECT_EQ(result->value, ssize_t(2));
      ASSERT_TRUE(f.engine.retire(*requests[i]));
    }
  }
}

TEST(LlvmLibcAioEngineTest, QueuedCancellationUnblocksNormalSameFdSuccessor) {
  const Op ops[] = {Op::Read, Op::Write, Op::Sync, Op::DataSync};
  const Op successors[] = {Op::Read, Op::Write};
  for (Op op : ops) {
    for (Op successor : successors) {
      Fixture f;
      ASSERT_TRUE(bool(f.engine.initialize(3)));
      auto first = f.make_request();
      auto second = f.make_request();
      int a = 0, b = 0;
      ASSERT_TRUE(bool(first.resources.capture(7)));
      ASSERT_TRUE(bool(second.resources.capture(7)));
      ASSERT_TRUE(bool(f.engine.admit(first, input(&a, op))));
      ASSERT_TRUE(bool(f.engine.admit(second, input(&b, successor))));
      ASSERT_TRUE(f.engine.cancel_queued(first));
      ASSERT_EQ(f.engine.progress(), 0);
      ASSERT_EQ(f.ring.tail, 1U);
      EXPECT_EQ(uint64_t(f.ring.sq[0].user_data),
                second.state().identity().token);
      EXPECT_FALSE(first.published());
      EXPECT_TRUE(second.published());
      EXPECT_EQ(*f.engine.error(&a), ECANCELED);
      EXPECT_EQ(*f.engine.error(&b), EINPROGRESS);
      f.ring.complete(0, 8);
      ASSERT_EQ(f.engine.progress(), 0);
      EXPECT_EQ(*f.engine.error(&b), 0);
      ASSERT_TRUE(bool(f.engine.take_result(&a)));
      ASSERT_TRUE(bool(f.engine.take_result(&b)));
      ASSERT_TRUE(f.engine.retire(first));
      ASSERT_TRUE(f.engine.retire(second));
    }
  }
}

TEST(LlvmLibcAioEngineTest, CanceledMiddleSyncDoesNotBypassEarlierOriginal) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(4)));
  auto first = f.make_request();
  auto sync = f.make_request();
  auto last = f.make_request();
  int a = 0, b = 0, c = 0;
  ASSERT_TRUE(bool(first.resources.capture(7)));
  ASSERT_TRUE(bool(sync.resources.capture(7)));
  ASSERT_TRUE(bool(last.resources.capture(7)));
  ASSERT_TRUE(bool(f.engine.admit(first, input(&a))));
  ASSERT_TRUE(bool(f.engine.admit(sync, input(&b, Op::DataSync))));
  ASSERT_TRUE(bool(f.engine.admit(last, input(&c, Op::Write))));
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(f.engine.cancel_queued(sync));
  ASSERT_TRUE(bool(f.engine.take_result(&b)));
  ASSERT_TRUE(f.engine.retire(sync));
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_EQ(f.ring.tail, 1U);
  EXPECT_FALSE(last.published());
  f.ring.complete(0, -EIO);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_EQ(f.ring.tail, 2U);
  EXPECT_TRUE(last.published());
  EXPECT_EQ(*f.engine.error(&c), EINPROGRESS);
  f.ring.complete(1, 8);
  ASSERT_EQ(f.engine.progress(), 0);
  EXPECT_EQ(*f.engine.error(&c), 0);
  ASSERT_TRUE(bool(f.engine.take_result(&a)));
  ASSERT_TRUE(bool(f.engine.take_result(&c)));
  ASSERT_TRUE(f.engine.retire(first));
  ASSERT_TRUE(f.engine.retire(last));
}

TEST(LlvmLibcAioEngineTest, PublicationRetryAndWithdrawalKeepSameFdOrder) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(3)));
  auto first = f.make_request();
  auto second = f.make_request();
  int a = 0, b = 0;
  ASSERT_TRUE(bool(first.resources.capture(7)));
  ASSERT_TRUE(bool(second.resources.capture(7)));
  ASSERT_TRUE(bool(f.engine.admit(first, input(&a, Op::Sync))));
  ASSERT_TRUE(bool(f.engine.admit(second, input(&b, Op::Write))));
  f.ring.enqueue_error = EAGAIN;
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_EQ(f.ring.tail, 0U);
  EXPECT_FALSE(first.published());
  EXPECT_FALSE(second.published());
  f.ring.enqueue_error = 0;
  f.ring.submit_limit = 0;
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_EQ(f.ring.tail, 1U);
  EXPECT_TRUE(first.published());
  EXPECT_FALSE(second.published());
  f.ring.submit_error = EIO;
  ASSERT_EQ(f.engine.progress(), EIO);
  ASSERT_EQ(f.ring.tail, 0U);
  EXPECT_TRUE(first.complete());
  EXPECT_FALSE(second.complete());
  EXPECT_EQ(*f.engine.error(&a), EIO);
  f.ring.submit_error = 0;
  f.ring.submit_limit = 64;
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_EQ(f.ring.tail, 1U);
  EXPECT_EQ(uint64_t(f.ring.sq[0].user_data), second.state().identity().token);
  EXPECT_EQ(*f.engine.error(&b), EINPROGRESS);
  f.ring.complete(0, 8);
  ASSERT_EQ(f.engine.progress(), 0);
  EXPECT_EQ(*f.engine.error(&b), 0);
  ASSERT_TRUE(bool(f.engine.take_result(&a)));
  ASSERT_TRUE(bool(f.engine.take_result(&b)));
  ASSERT_TRUE(f.engine.retire(first));
  ASSERT_TRUE(f.engine.retire(second));
}

TEST(LlvmLibcAioEngineTest, ShortSubmissionDoesNotRepublish) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(3)));
  auto first = f.make_request();
  auto second = f.make_request();
  int a = 0, b = 0;
  ASSERT_TRUE(bool(first.resources.capture(7)));
  // Independent fds keep this a two-SQE short-submit test under fd ordering.
  ASSERT_TRUE(bool(second.resources.capture(8)));
  ASSERT_TRUE(bool(f.engine.admit(first, input(&a))));
  ASSERT_TRUE(bool(f.engine.admit(second, input(&b, Op::Read, false, 8))));
  f.ring.submit_limit = 1;
  ASSERT_EQ(f.engine.progress(), 0);
  EXPECT_EQ(f.ring.head, 1U);
  EXPECT_EQ(f.ring.tail, 2U);
  ASSERT_EQ(f.engine.progress(), 0);
  EXPECT_EQ(f.ring.head, 2U);
  EXPECT_EQ(f.ring.tail, 2U);
  EXPECT_FALSE(bool(f.engine.cancel_queued(first)));
  EXPECT_FALSE(bool(f.engine.cancel_queued(second)));
  f.ring.complete(0, 8);
  f.ring.complete(1, 8);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(bool(f.engine.take_result(&a)));
  ASSERT_TRUE(bool(f.engine.take_result(&b)));
  ASSERT_TRUE(bool(f.engine.retire(first)));
  ASSERT_TRUE(bool(f.engine.retire(second)));
}

TEST(LlvmLibcAioEngineTest, EnterFailureWithdrawsOnlyUnconsumedTail) {
  for (unsigned consumed = 0; consumed != 3; ++consumed) {
    Fixture f;
    ASSERT_TRUE(bool(f.engine.initialize(3)));
    auto first = f.make_request();
    auto second = f.make_request();
    int a = 0, b = 0;
    ASSERT_TRUE(bool(first.resources.capture(7)));
    // Independent fds preserve the consumed-prefix/unconsumed-tail matrix.
    ASSERT_TRUE(bool(second.resources.capture(8)));
    ASSERT_TRUE(bool(f.engine.admit(first, input(&a))));
    ASSERT_TRUE(bool(f.engine.admit(second, input(&b, Op::Read, false, 8))));
    f.ring.submit_limit = consumed;
    f.ring.submit_error = EINTR;
    ASSERT_EQ(f.engine.progress(), EINTR);
    EXPECT_EQ(f.ring.tail, consumed);
    EXPECT_EQ(first.complete(), consumed == 0);
    EXPECT_EQ(second.complete(), consumed < 2);
    f.ring.submit_error = 0;
    for (unsigned i = 0; i < consumed; ++i)
      f.ring.complete(i, 8);
    ASSERT_EQ(f.engine.progress(), 0);
    auto first_result = f.engine.take_result(&a);
    auto second_result = f.engine.take_result(&b);
    ASSERT_TRUE(bool(first_result));
    ASSERT_TRUE(bool(second_result));
    EXPECT_EQ(first_result->error, consumed == 0 ? EINTR : 0);
    EXPECT_EQ(second_result->error, consumed < 2 ? EINTR : 0);
    ASSERT_TRUE(bool(f.engine.retire(first)));
    ASSERT_TRUE(bool(f.engine.retire(second)));
  }
}

TEST(LlvmLibcAioEngineTest,
     SameOriginalFdGroupsDespiteDifferentPrivateDuplicates) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(4)));
  auto same = f.make_request();
  auto other = f.make_request();
  auto sync = f.make_request();
  int a = 0, b = 0, c = 0;
  ASSERT_TRUE(bool(same.resources.capture(7)));
  ASSERT_TRUE(bool(other.resources.capture(8)));
  ASSERT_TRUE(bool(sync.resources.capture(7)));
  ASSERT_TRUE(bool(f.engine.admit(same, input(&a, Op::Write))));
  ASSERT_TRUE(bool(f.engine.admit(other, input(&b, Op::Read, false, 8))));
  ASSERT_TRUE(bool(f.engine.admit(sync, input(&c, Op::DataSync))));
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_EQ(f.ring.tail, 2U);
  EXPECT_FALSE(bool(sync.published()));
  f.ring.complete(0, 8);
  ASSERT_EQ(f.engine.progress(), 0);
  EXPECT_TRUE(bool(sync.published()));
  EXPECT_FALSE(bool(other.complete()));
  ASSERT_EQ(f.ring.tail, 3U);
  EXPECT_EQ(f.ring.sq[2].opcode, static_cast<unsigned char>(IORING_OP_FSYNC));
  EXPECT_EQ(f.ring.sq[2].fsync_flags, unsigned(IORING_FSYNC_DATASYNC));
  EXPECT_NE(f.ring.sq[0].fd, f.ring.sq[2].fd);
  ASSERT_TRUE(bool(f.engine.take_result(&a)));
  ASSERT_TRUE(bool(f.engine.retire(same)));
  f.ring.complete(2, 0);
  f.ring.complete(1, 8);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(bool(f.engine.take_result(&b)));
  ASSERT_TRUE(bool(f.engine.take_result(&c)));
  ASSERT_TRUE(bool(f.engine.retire(other)));
  ASSERT_TRUE(bool(f.engine.retire(sync)));
}

TEST(LlvmLibcAioEngineTest, FailedPredecessorSurvivesReturnUntilSyncDecision) {
  const Op sync_ops[] = {Op::Sync, Op::DataSync};
  for (Op sync_op : sync_ops) {
    Fixture f;
    ASSERT_TRUE(bool(f.engine.initialize(4)));
    auto first = f.make_request();
    auto second = f.make_request();
    auto sync = f.make_request();
    int a = 0, b = 0, c = 0;
    ASSERT_TRUE(bool(first.resources.capture(7)));
    ASSERT_TRUE(bool(second.resources.capture(7)));
    ASSERT_TRUE(bool(sync.resources.capture(7)));
    ASSERT_TRUE(bool(f.engine.admit(first, input(&a, Op::Write))));
    ASSERT_TRUE(bool(f.engine.admit(second, input(&b, Op::Write))));
    ASSERT_TRUE(bool(f.engine.admit(sync, input(&c, sync_op))));
    ASSERT_EQ(f.engine.progress(), 0);
    // Same-fd originals now execute in admission order, not either CQE order.
    ASSERT_EQ(f.ring.tail, 1U);
    EXPECT_FALSE(second.published());
    f.ring.complete(0, -ENOSPC);
    ASSERT_EQ(f.engine.progress(), 0);
    ASSERT_EQ(f.ring.tail, 2U);
    EXPECT_TRUE(second.published());
    EXPECT_EQ(*f.engine.error(&b), EINPROGRESS);
    ASSERT_TRUE(bool(f.engine.take_result(&a)));
    // The other same-fd predecessor keeps this decision retryable.
    EXPECT_FALSE(bool(f.engine.retire(first)));
    EXPECT_EQ(*f.engine.error(&c), EINPROGRESS);
    f.ring.complete(1, -EIO);
    ASSERT_EQ(f.engine.progress(), 0);
    EXPECT_FALSE(bool(sync.published()));
    EXPECT_EQ(f.ring.tail, 2U);
    EXPECT_EQ(*f.engine.error(&c), ENOSPC);
    ASSERT_TRUE(bool(f.engine.take_result(&b)));
    ASSERT_TRUE(bool(f.engine.take_result(&c)));
    ASSERT_TRUE(bool(f.engine.retire(first)));
    ASSERT_TRUE(bool(f.engine.retire(second)));
    ASSERT_TRUE(bool(f.engine.retire(sync)));
  }
}

TEST(LlvmLibcAioEngineTest, DifferentNumericAliasDoesNotGroupOrPropagateError) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(3)));
  auto alias = f.make_request();
  auto sync = f.make_request();
  int a = 0, b = 0;
  // Model fd 8 as a caller-owned dup of fd 7; only the original number groups.
  ASSERT_TRUE(bool(alias.resources.capture(8)));
  ASSERT_TRUE(bool(sync.resources.capture(7)));
  ASSERT_TRUE(bool(f.engine.admit(alias, input(&a, Op::Write, false, 8))));
  ASSERT_TRUE(bool(f.engine.admit(sync, input(&b, Op::Sync))));
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_EQ(f.ring.tail, 2U);
  EXPECT_TRUE(sync.published());
  EXPECT_EQ(*f.engine.error(&a), EINPROGRESS);
  EXPECT_EQ(f.ring.sq[1].opcode, static_cast<unsigned char>(IORING_OP_FSYNC));
  f.ring.complete(0, -EIO);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(bool(f.engine.take_result(&a)));
  ASSERT_TRUE(bool(f.engine.retire(alias)));
  f.ring.complete(1, 0);
  ASSERT_EQ(f.engine.progress(), 0);
  EXPECT_EQ(*f.engine.error(&b), 0);
  ASSERT_TRUE(bool(f.engine.take_result(&b)));
  ASSERT_TRUE(bool(f.engine.retire(sync)));
}

TEST(LlvmLibcAioEngineTest, QueuedSyncCancelReleasesUnpublishedCredit) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(3)));
  auto prior = f.make_request();
  auto sync = f.make_request();
  int a = 0, b = 0;
  ASSERT_TRUE(bool(prior.resources.capture(7)));
  ASSERT_TRUE(bool(sync.resources.capture(7)));
  ASSERT_TRUE(bool(f.engine.admit(prior, input(&a))));
  ASSERT_TRUE(bool(f.engine.admit(sync, input(&b, Op::Sync))));
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_EQ(f.ring.tail, 1U);
  ASSERT_TRUE(bool(f.engine.cancel_queued(sync)));
  EXPECT_EQ(*f.engine.error(&b), ECANCELED);
  ASSERT_TRUE(bool(f.engine.take_result(&b)));
  ASSERT_TRUE(bool(f.engine.retire(sync)));
  EXPECT_EQ(*f.engine.error(&a), EINPROGRESS);
  f.ring.complete(0, 8);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(bool(f.engine.take_result(&a)));
  ASSERT_TRUE(bool(f.engine.retire(prior)));
}

TEST(LlvmLibcAioEngineTest, AdmissionReservesControlHeadroom) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(2)));
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

TEST(LlvmLibcAioEngineTest, MinimumCapacityAndOriginalFdValidation) {
  Fixture f;
  EXPECT_EQ(f.engine.initialize(0).error(), EINVAL);
  EXPECT_EQ(f.engine.initialize(1).error(), EINVAL);
  ASSERT_TRUE(bool(f.engine.initialize(2)));
  auto r = f.make_request();
  int cb = 0;
  ASSERT_TRUE(bool(r.resources.capture(0)));
  EXPECT_EQ(f.engine.admit(r, input(&cb, Op::Read, false, -1)).error(), EINVAL);
  EXPECT_EQ(f.engine.admit(r, input(&cb, Op::Sync, false, -2)).error(), EINVAL);
  EXPECT_EQ(f.watermark, uint64_t(0));
  EXPECT_TRUE(f.engine.empty());
  EXPECT_EQ(f.engine.find(&cb), static_cast<Engine::Request *>(nullptr));
  ASSERT_TRUE(bool(f.engine.admit(r, input(&cb, Op::Read, false, 0))));
  ASSERT_TRUE(f.engine.cancel_queued(r));
  ASSERT_TRUE(bool(f.engine.take_result(&cb)));
  ASSERT_TRUE(f.engine.retire(r));
}

TEST(LlvmLibcAioEngineTest, LaterSameFdWorkAndErrorsNeverEnterSyncSnapshot) {
  const bool outcomes[] = {false, true};
  for (bool later_fails : outcomes) {
    Fixture f;
    ASSERT_TRUE(bool(f.engine.initialize(4)));
    auto prior = f.make_request();
    auto sync = f.make_request();
    auto later = f.make_request();
    int a = 0, b = 0, c = 0;
    ASSERT_TRUE(bool(prior.resources.capture(7)));
    ASSERT_TRUE(bool(sync.resources.capture(7)));
    ASSERT_TRUE(bool(later.resources.capture(7)));
    ASSERT_TRUE(bool(f.engine.admit(prior, input(&a, Op::Write))));
    ASSERT_TRUE(bool(f.engine.admit(sync, input(&b, Op::Sync))));
    ASSERT_TRUE(bool(f.engine.admit(later, input(&c, Op::Write))));
    ASSERT_EQ(f.engine.progress(), 0);
    ASSERT_EQ(f.ring.tail, 1U);
    if (later_fails) {
      // A later original cannot publish ahead of the sync; queued cancellation
      // still supplies a later error before the sync makes its decision.
      ASSERT_TRUE(f.engine.cancel_queued(later));
    }
    EXPECT_FALSE(sync.published());
    f.ring.complete(0, 8);
    ASSERT_EQ(f.engine.progress(), 0);
    ASSERT_EQ(f.ring.tail, 2U);
    EXPECT_TRUE(sync.published());
    EXPECT_FALSE(later.published());
    EXPECT_EQ(*f.engine.error(&c), later_fails ? ECANCELED : EINPROGRESS);
    f.ring.complete(1, 0);
    ASSERT_EQ(f.engine.progress(), 0);
    EXPECT_EQ(*f.engine.error(&b), 0);
    if (!later_fails) {
      ASSERT_EQ(f.ring.tail, 3U);
      EXPECT_TRUE(later.published());
      f.ring.complete(2, 8);
      ASSERT_EQ(f.engine.progress(), 0);
    }
    ASSERT_TRUE(bool(f.engine.take_result(&a)));
    ASSERT_TRUE(bool(f.engine.take_result(&b)));
    ASSERT_TRUE(bool(f.engine.take_result(&c)));
    ASSERT_TRUE(f.engine.retire(prior));
    ASSERT_TRUE(f.engine.retire(sync));
    ASSERT_TRUE(f.engine.retire(later));
  }
}

TEST(LlvmLibcAioEngineTest, UnrelatedSyncDoesNotRetainFailedResult) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(4)));
  auto prior = f.make_request();
  auto other = f.make_request();
  auto sync = f.make_request();
  int a = 0, b = 0, c = 0;
  ASSERT_TRUE(bool(prior.resources.capture(7)));
  ASSERT_TRUE(bool(other.resources.capture(8)));
  ASSERT_TRUE(bool(sync.resources.capture(7)));
  ASSERT_TRUE(bool(f.engine.admit(prior, input(&a, Op::Write))));
  ASSERT_TRUE(bool(f.engine.admit(other, input(&b, Op::Write, false, 8))));
  ASSERT_TRUE(bool(f.engine.admit(sync, input(&c, Op::Sync))));
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_EQ(f.ring.tail, 2U);
  f.ring.complete(1, -EIO);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(bool(f.engine.take_result(&b)));
  ASSERT_TRUE(f.engine.retire(other));
  EXPECT_FALSE(sync.published());
  EXPECT_EQ(*f.engine.error(&c), EINPROGRESS);
  f.ring.complete(0, 8);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_EQ(f.ring.tail, 3U);
  f.ring.complete(2, 0);
  ASSERT_EQ(f.engine.progress(), 0);
  EXPECT_EQ(*f.engine.error(&c), 0);
  ASSERT_TRUE(bool(f.engine.take_result(&a)));
  ASSERT_TRUE(bool(f.engine.take_result(&c)));
  ASSERT_TRUE(f.engine.retire(prior));
  ASSERT_TRUE(f.engine.retire(sync));
}

TEST(LlvmLibcAioEngineTest, FailureTerminalBeforeSyncAdmissionIsExcluded) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(2)));
  auto prior = f.make_request();
  auto sync = f.make_request();
  int a = 0, b = 0;
  ASSERT_TRUE(bool(prior.resources.capture(7)));
  ASSERT_TRUE(bool(sync.resources.capture(7)));
  ASSERT_TRUE(bool(f.engine.admit(prior, input(&a, Op::Write))));
  ASSERT_EQ(f.engine.progress(), 0);
  f.ring.complete(0, -EIO);
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_TRUE(bool(f.engine.admit(sync, input(&b, Op::Sync))));
  ASSERT_EQ(f.engine.progress(), 0);
  ASSERT_EQ(f.ring.tail, 2U);
  EXPECT_TRUE(sync.published());
  EXPECT_EQ(*f.engine.error(&a), EIO);
  f.ring.complete(1, 0);
  ASSERT_EQ(f.engine.progress(), 0);
  EXPECT_EQ(*f.engine.error(&b), 0);
  ASSERT_TRUE(bool(f.engine.take_result(&a)));
  ASSERT_TRUE(bool(f.engine.take_result(&b)));
  ASSERT_TRUE(f.engine.retire(prior));
  ASSERT_TRUE(f.engine.retire(sync));
}

TEST(LlvmLibcAioEngineTest, RetainedResultsDoNotConsumeCompletionCredits) {
  Fixture f;
  ASSERT_TRUE(bool(f.engine.initialize(2)));
  struct Allocator {
    bool fail = false;
    void *allocate(size_t size, size_t alignment) {
      return fail ? nullptr : ::aligned_alloc(alignment, size);
    }
    void deallocate(void *p) { ::free(p); }
  } allocator;
  using Storage = LIBC_NAMESPACE::AioStorage<Engine::Request, 2, Allocator>;
  Storage storage;
  Storage::Slot *slots[6]{};
  Engine::Request *requests[6]{};
  int cb[6]{};
  for (unsigned i = 0; i < 6; ++i) {
    if (i % 2 == 0) {
      if (i) {
        allocator.fail = true;
        EXPECT_EQ(Storage::prepare(allocator), nullptr);
        EXPECT_EQ(*f.engine.error(&cb[0]), 0);
        EXPECT_EQ(storage.size(), size_t(i));
        allocator.fail = false;
      }
      auto *block = Storage::prepare(allocator);
      ASSERT_NE(block, nullptr);
      ASSERT_TRUE(storage.publish(*block));
    }
    slots[i] = storage.acquire();
    ASSERT_NE(slots[i], nullptr);
    requests[i] =
        new (slots[i]->storage()) Engine::Request(f.ring, {&f.resources});
    ASSERT_TRUE(bool(requests[i]->resources.capture(7)));
    ASSERT_TRUE(bool(f.engine.admit(*requests[i], input(&cb[i]))));
    ASSERT_EQ(f.engine.progress(), 0);
    f.ring.complete(i, int(i));
    ASSERT_EQ(f.engine.progress(), 0);
    EXPECT_FALSE(bool(f.engine.retire(*requests[i])));
    EXPECT_EQ(storage.detach_empty(), nullptr);
  }
  // More live result records than total CQ credits, without consuming any
  // result to admit a successor. Every old binding remains independently live.
  for (unsigned i = 0; i < 6; ++i) {
    EXPECT_EQ(*f.engine.error(&cb[i]), 0);
    auto result = f.engine.take_result(&cb[i]);
    ASSERT_TRUE(bool(result));
    EXPECT_EQ(result->value, ssize_t(i));
    ASSERT_TRUE(bool(f.engine.retire(*requests[i])));
    requests[i]->~Request();
    ASSERT_TRUE(storage.release(*slots[i]));
  }
  while (auto *block = storage.detach_empty())
    ASSERT_TRUE(Storage::dispose(block));
  EXPECT_EQ(storage.capacity(), size_t(0));
  EXPECT_EQ(f.resources.closed, 6U);
  EXPECT_TRUE(f.engine.empty());
}

TEST(LlvmLibcAioEngineTest, SuspendObservesOriginalNotCancelControl) {
  using Observers = LIBC_NAMESPACE::AioSuspendObservers<int>;
  FakeRing ring;
  ResourceState resources;
  uint64_t watermark = 0;
  Observers observers;
  auto complete = [](void *p, const void *cb) {
    static_cast<Observers *>(p)->complete(cb);
  };
  Engine engine(ring, watermark, 17, 16, complete, &observers);
  ASSERT_TRUE(bool(engine.initialize(2)));
  Engine::Request request(ring, {&resources}), fresh(ring, {&resources});
  Engine::Cancel cancel;
  int cb;
  const int *list[] = {&cb};
  Observers::Observer old_wait, new_wait;
  unsigned old_wakes = 0, new_wakes = 0;
  auto wake = [](void *p) { ++*static_cast<unsigned *>(p); };
  auto query = [&](const int *p) -> ErrorOr<bool> {
    auto *r = engine.find(p);
    return r ? ErrorOr<bool>(r->complete()) : ErrorOr<bool>(Error(EINVAL));
  };
  ASSERT_TRUE(bool(request.resources.capture(7)));
  ASSERT_TRUE(bool(engine.admit(request, input(&cb))));
  ASSERT_TRUE(
      bool(observers.attach(old_wait, list, 1, query, wake, &old_wakes)));
  ASSERT_EQ(engine.progress(), 0);
  ASSERT_TRUE(bool(engine.cancel_published(request, cancel)));
  ASSERT_EQ(engine.progress(), 0);
  ring.complete(1, 0);
  ASSERT_EQ(engine.progress(), 0);
  EXPECT_FALSE(old_wait.ready());
  EXPECT_EQ(old_wakes, 0U);
  ring.complete(0, -ECANCELED);
  ASSERT_EQ(engine.progress(), 0);
  EXPECT_TRUE(old_wait.ready());
  EXPECT_EQ(old_wakes, 1U);
  ASSERT_TRUE(engine.retire_cancel(cancel));
  ASSERT_TRUE(bool(engine.take_result(&cb)));
  ASSERT_TRUE(bool(engine.retire(request)));
  ASSERT_TRUE(bool(fresh.resources.capture(8)));
  ASSERT_TRUE(bool(engine.admit(fresh, input(&cb))));
  ASSERT_TRUE(
      bool(observers.attach(new_wait, list, 1, query, wake, &new_wakes)));
  EXPECT_FALSE(new_wait.ready());
  EXPECT_TRUE(old_wait.ready());
  ASSERT_TRUE(engine.cancel_queued(fresh));
  EXPECT_TRUE(new_wait.ready());
  EXPECT_EQ(new_wakes, 1U);
  EXPECT_EQ(old_wakes, 1U);
  ASSERT_TRUE(bool(engine.take_result(&cb)));
  ASSERT_TRUE(bool(engine.retire(fresh)));
  EXPECT_TRUE(observers.detach(old_wait));
  EXPECT_TRUE(observers.detach(new_wait));
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
  f.ring.complete(0, 8);
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
    auto next = f.make_request();
    Engine::Cancel cancel;
    int cb = 0, next_cb = 0;
    ASSERT_TRUE(bool(r.resources.capture(7)));
    ASSERT_TRUE(bool(next.resources.capture(7)));
    ASSERT_TRUE(bool(f.engine.admit(r, input(&cb))));
    ASSERT_TRUE(bool(f.engine.admit(next, input(&next_cb, Op::Write))));
    ASSERT_EQ(f.engine.progress(), 0);
    ASSERT_TRUE(bool(f.engine.cancel_published(r, cancel)));
    ASSERT_EQ(f.engine.progress(), 0);
    ASSERT_EQ(f.ring.tail, 2U);
    EXPECT_EQ(f.ring.sq[1].opcode,
              static_cast<unsigned char>(IORING_OP_ASYNC_CANCEL));
    EXPECT_EQ(uint64_t(f.ring.sq[1].addr), r.state().identity().token);
    f.ring.complete(cancel_first ? 1 : 0, cancel_first ? 0 : -ECANCELED);
    ASSERT_EQ(f.engine.progress(), 0);
    EXPECT_EQ(r.complete(), !cancel_first);
    EXPECT_EQ(cancel.complete(), cancel_first);
    EXPECT_EQ(next.published(), !cancel_first);
    EXPECT_EQ(f.ring.tail, cancel_first ? 2U : 3U);
    if (cancel_first) {
      EXPECT_EQ(f.engine.original_result(cancel).error(), EINPROGRESS);
      EXPECT_EQ(*f.engine.cancel_result(cancel), 0);
    } else {
      ASSERT_TRUE(bool(f.engine.take_result(&cb)));
      EXPECT_FALSE(f.engine.retire(r));
    }
    f.ring.complete(cancel_first ? 0 : 1, cancel_first ? -ECANCELED : -ENOENT);
    ASSERT_EQ(f.engine.progress(), 0);
    ASSERT_EQ(f.ring.tail, 3U);
    EXPECT_TRUE(next.published());
    EXPECT_EQ(*f.engine.error(&next_cb), EINPROGRESS);
    f.ring.complete(2, 8);
    ASSERT_EQ(f.engine.progress(), 0);
    EXPECT_EQ(*f.engine.error(&next_cb), 0);
    auto original = f.engine.original_result(cancel);
    ASSERT_TRUE(bool(original));
    EXPECT_EQ(original->error, ECANCELED);
    if (cancel_first)
      ASSERT_TRUE(bool(f.engine.take_result(&cb)));
    EXPECT_FALSE(f.engine.retire(r));
    ASSERT_TRUE(f.engine.retire_cancel(cancel));
    ASSERT_TRUE(f.engine.retire(r));
    ASSERT_TRUE(bool(f.engine.take_result(&next_cb)));
    ASSERT_TRUE(f.engine.retire(next));
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
  f.ring.complete(0, 8);
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
  f.ring.complete(0, 8);
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
  f.ring.complete(0, 8);
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
  f.ring.complete(0, 8);
  ASSERT_EQ(f.engine.progress(), 0);
  EXPECT_EQ(f.ring.tail, 1U);
  EXPECT_TRUE(cancel.complete());
  EXPECT_EQ(f.engine.cancel_result(cancel).error(), EALREADY);
  auto result = f.engine.take_result(&cb);
  ASSERT_TRUE(bool(result));
  EXPECT_EQ(result->value, ssize_t(8));
  ASSERT_TRUE(f.engine.retire(r));
  ASSERT_TRUE(f.engine.retire_cancel(cancel));
}

TEST(LlvmLibcAioEngineTest, LastOriginalTokenSucceedsThenExhaustionNeverWraps) {
  FakeRing ring;
  ResourceState resources;
  uint64_t watermark = UINT64_MAX - 1;
  Engine engine(ring, watermark, 17, 16);
  ASSERT_TRUE(bool(engine.initialize(3)));
  Engine::Request last(ring, {&resources}), blocked(ring, {&resources});
  int a = 0, b = 0;
  ASSERT_TRUE(bool(last.resources.capture(7)));
  ASSERT_TRUE(bool(blocked.resources.capture(7)));
  ASSERT_TRUE(bool(engine.admit(last, input(&a))));
  EXPECT_EQ(watermark, UINT64_MAX);
  EXPECT_EQ(last.state().identity().token, UINT64_MAX);
  EXPECT_EQ(engine.admit(blocked, input(&b)).error(), EOVERFLOW);
  EXPECT_EQ(engine.find(&b), static_cast<Engine::Request *>(nullptr));
  EXPECT_EQ(ring.tail, 0U);
  ASSERT_TRUE(engine.cancel_queued(last));
  ASSERT_TRUE(bool(engine.take_result(&a)));
  ASSERT_TRUE(engine.retire(last));
  EXPECT_EQ(engine.admit(blocked, input(&b)).error(), EOVERFLOW);
  EXPECT_EQ(watermark, UINT64_MAX);
  ASSERT_TRUE(bool(blocked.resources.release()));
  EXPECT_EQ(resources.closed, 2U);
  EXPECT_TRUE(engine.empty());
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
