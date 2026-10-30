//===-- Tests for MMIX AIO service record ownership -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/func/aligned_alloc.h"
#include "src/__support/threads/linux/mmix/aio_service_registry.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::AioFileDependencies;
using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;

namespace {
// Use the production signal-masking gate, with assertions about which side of
// it each injected operation runs. These are scripted orders, not an SMP test.
struct CheckedGate {
  LIBC_NAMESPACE::mmix::AioGate gate;
  static inline bool held = false;
  struct Guard {
    LIBC_NAMESPACE::mmix::AioGate::Guard guard;
    explicit Guard(CheckedGate &gate) : guard(gate.gate) {
      EXPECT_FALSE(held);
      held = bool(guard);
    }
    ~Guard() { held = false; }
    explicit operator bool() const { return bool(guard); }
    int error() const { return guard.error(); }
  };
};

struct FakeRing {
  io_uring_sqe sq[64]{};
  io_uring_cqe cq[64]{};
  unsigned head = 0, tail = 0, cq_head = 0, cq_tail = 0;
  unsigned next_personality = 1, personalities = 0, submits = 0;
  unsigned unregisters = 0;
  int unregister_error = 0, submit_error = 0;
  void (*on_submit)(void *) = nullptr;
  void *context = nullptr;

  ErrorOr<unsigned> register_personality() {
    EXPECT_FALSE(CheckedGate::held);
    ++personalities;
    return next_personality++;
  }
  ErrorOr<void> unregister_personality(unsigned) {
    EXPECT_FALSE(CheckedGate::held);
    ++unregisters;
    if (unregister_error)
      return Error(unregister_error);
    --personalities;
    return {};
  }
  ErrorOr<void> check_integrity() { return {}; }
  ErrorOr<void> enqueue(const io_uring_sqe &entry) {
    EXPECT_TRUE(CheckedGate::held);
    if (tail == 64)
      return Error(EAGAIN);
    sq[tail++] = entry;
    return {};
  }
  ErrorOr<unsigned> submit() {
    EXPECT_FALSE(CheckedGate::held);
    ++submits;
    if (on_submit)
      on_submit(context);
    if (submit_error)
      return Error(submit_error);
    unsigned count = tail - head;
    head = tail;
    return count;
  }
  ErrorOr<void> withdraw_last(uint64_t token) {
    EXPECT_TRUE(CheckedGate::held);
    if (head == tail)
      return Error(EAGAIN);
    if (sq[tail - 1].user_data != token)
      return Error(EINVAL);
    --tail;
    return {};
  }
  ErrorOr<void> poll(io_uring_cqe &result) {
    EXPECT_TRUE(CheckedGate::held);
    if (cq_head == cq_tail)
      return Error(EAGAIN);
    result = cq[cq_head++];
    return {};
  }
  void complete(unsigned index, int result) {
    cq[cq_tail++] = {sq[index].user_data, result, 0};
  }
  void identify(unsigned index, uint64_t inode = 1) {
    auto *stat = reinterpret_cast<LIBC_NAMESPACE::internal::kernel_statx_buf *>(
        uintptr_t(sq[index].off));
    stat->stx_ino = inode;
    complete(index, 0);
  }
};

struct ResourceState {
  unsigned closed = 0, duplicated = 0;
  int duplicate_error = 0, close_error = 0;
  void (*on_close)(void *) = nullptr;
  void *context = nullptr;
};
struct ResourceOps {
  ResourceState *state;
  ErrorOr<int> duplicate(int fd) {
    EXPECT_FALSE(CheckedGate::held);
    ++state->duplicated;
    if (state->duplicate_error)
      return Error(state->duplicate_error);
    return fd + 100;
  }
  ErrorOr<int> close(int) {
    EXPECT_FALSE(CheckedGate::held);
    ++state->closed;
    if (state->on_close)
      state->on_close(state->context);
    return state->close_error ? ErrorOr<int>(Error(state->close_error))
                              : ErrorOr<int>(0);
  }
};

struct Allocator {
  bool fail = false;
  unsigned allocated = 0, freed = 0;
  void *allocate(size_t size, size_t alignment) {
    EXPECT_FALSE(CheckedGate::held);
    if (fail)
      return nullptr;
    ++allocated;
    return ::aligned_alloc(alignment, size);
  }
  void deallocate(void *p) {
    EXPECT_FALSE(CheckedGate::held);
    ++freed;
    ::free(p);
  }
};

struct NotificationState {
  int prepare_error = 0, activate_error = 0, cleanup_error = 0;
  unsigned prepared = 0, activated = 0, discarded = 0, cleaned = 0;
  unsigned destroyed = 0;
  bool callback_resources = false;
  void *owner = nullptr;
  void (*on_cleanup)(void *) = nullptr;
  void *context = nullptr;
};
struct Notification {
  NotificationState *state = nullptr;
  bool prepared = false, resources = false;

  int prepare(NotificationState &s) {
    EXPECT_FALSE(CheckedGate::held);
    state = &s;
    s.owner = this;
    ++s.prepared;
    resources = true;
    prepared = !s.prepare_error;
    return s.prepare_error;
  }
  long activate() {
    EXPECT_FALSE(CheckedGate::held);
    EXPECT_EQ(state->owner, static_cast<void *>(this));
    EXPECT_TRUE(prepared);
    ++state->activated;
    if (state->activate_error != EAGAIN && state->activate_error != EBUSY)
      prepared = false;
    return state->activate_error;
  }
  void discard() {
    EXPECT_FALSE(CheckedGate::held);
    if (state)
      ++state->discarded;
    prepared = false;
  }
  long cleanup() {
    EXPECT_FALSE(CheckedGate::held);
    if (!state)
      return 0;
    ++state->cleaned;
    if (state->on_cleanup)
      state->on_cleanup(state->context);
    if (prepared || state->callback_resources)
      return EBUSY;
    if (state->cleanup_error)
      return state->cleanup_error;
    resources = false;
    return 0;
  }
  bool drained() {
    return !prepared && !resources && (!state || !state->callback_resources);
  }
  ~Notification() {
    EXPECT_FALSE(CheckedGate::held);
    EXPECT_TRUE(drained());
    if (state)
      ++state->destroyed;
  }
};

using Service = LIBC_NAMESPACE::mmix::AioServiceRegistry<
    Allocator, 2, FakeRing, ResourceOps, Notification, CheckedGate>;
using Op = Service::Engine::Operation;
ErrorOr<AioFileDependencies::Identity>
classify(void *, int, const LIBC_NAMESPACE::internal::kernel_statx_buf &s) {
  EXPECT_TRUE(CheckedGate::held);
  return AioFileDependencies::Identity{1, 2, s.stx_ino,
                                       AioFileDependencies::FileType::Regular};
}
Service::Input input(const void *address, Op op = Op::Read) {
  return {address, 7, op, nullptr, 8, 42, false};
}
struct Fixture {
  FakeRing ring;
  Allocator allocator;
  ResourceState resources;
  uint64_t watermark = 0;
  Service service{ring, allocator, {&resources}, watermark, 17, classify};

  ErrorOr<void> admit(const void *cb, NotificationState &notification) {
    return service.admit(
        input(cb), [&](Notification &n) { return n.prepare(notification); });
  }
  void finish(unsigned metadata, int result = 8) {
    ring.identify(metadata);
    ring.complete(metadata + 1, result);
  }
  ~Fixture() {
    EXPECT_EQ(service.retained(), size_t(0));
    EXPECT_EQ(ring.personalities, 0U);
    EXPECT_EQ(allocator.allocated, allocator.freed);
  }
};
} // namespace

TEST(LlvmLibcAioServiceRegistryTest, GrowthOomAndResultsBeyondCreditCount) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(3)));
  int cb[6]{};
  NotificationState notifications[6];
  f.allocator.fail = true;
  EXPECT_EQ(f.admit(&cb[0], notifications[0]).error(), ENOMEM);
  EXPECT_EQ(f.resources.duplicated, 0U);
  EXPECT_EQ(f.service.retained(), size_t(0));
  f.allocator.fail = false;
  for (unsigned i = 0; i < 6; ++i) {
    if (i == 2) {
      f.allocator.fail = true;
      EXPECT_EQ(f.admit(&cb[i], notifications[i]).error(), ENOMEM);
      EXPECT_EQ(*f.service.error(&cb[0]), 0);
      EXPECT_EQ(*f.service.error(&cb[1]), 0);
      f.allocator.fail = false;
    }
    ASSERT_TRUE(bool(f.admit(&cb[i], notifications[i])));
    EXPECT_EQ(f.ring.submits, i * 2);
    ASSERT_EQ(f.service.submit(), 0);
    f.finish(i * 2, int(i));
    ASSERT_EQ(f.service.submit(), 0);
    ASSERT_EQ(f.service.maintain(), 0);
    EXPECT_EQ(f.service.retained(), size_t(i + 1));
    EXPECT_EQ(notifications[i].destroyed, 0U);
  }
  EXPECT_EQ(f.allocator.allocated, 3U);
  for (unsigned i = 0; i < 6; ++i) {
    auto result = f.service.take_result(&cb[i]);
    ASSERT_TRUE(bool(result));
    EXPECT_EQ(result->value, ssize_t(i));
  }
  EXPECT_EQ(f.resources.closed, 0U); // Queries do not physically retire.
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.resources.closed, 6U);
}

TEST(LlvmLibcAioServiceRegistryTest, FailedCaptureRetainsUnregisterForRetry) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(3)));
  NotificationState notification;
  int cb = 0;
  f.resources.duplicate_error = EMFILE;
  f.ring.unregister_error = EBUSY;
  EXPECT_EQ(f.admit(&cb, notification).error(), EMFILE);
  EXPECT_EQ(notification.prepared, 0U);
  EXPECT_EQ(f.service.error(&cb).error(), EINVAL);
  EXPECT_EQ(f.service.retained(), size_t(1));
  EXPECT_EQ(f.ring.unregisters, 1U); // Capture's first rollback failed.
  EXPECT_EQ(f.service.maintain(), EBUSY);
  EXPECT_EQ(f.service.maintain(), EBUSY);
  EXPECT_EQ(f.ring.unregisters, 3U);
  EXPECT_EQ(f.ring.personalities, 1U);
  EXPECT_EQ(f.resources.closed, 0U);
  f.ring.unregister_error = 0;
  EXPECT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.ring.submits, 0U);
}

TEST(LlvmLibcAioServiceRegistryTest, LocalCancelPinsConsumedOriginal) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(3)));
  NotificationState notification;
  int cb = 0;
  Service::CancelObservation observation;
  EXPECT_EQ(f.service.retain_cancel(&cb, observation), EINVAL);
  ASSERT_TRUE(bool(f.admit(&cb, notification)));
  ASSERT_EQ(f.service.retain_cancel(&cb, observation), 0);
  EXPECT_EQ(f.service.original_cancel_result(observation).error(), EINPROGRESS);
  ASSERT_EQ(f.service.request_cancel(observation), 0);
  EXPECT_EQ(f.service.request_cancel(observation), EINVAL);
  auto result = f.service.take_result(&cb);
  ASSERT_TRUE(bool(result));
  EXPECT_EQ(result->error, ECANCELED);
  EXPECT_EQ(f.ring.tail, 0U);
  EXPECT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.service.retained(), size_t(1));
  result = f.service.original_cancel_result(observation);
  ASSERT_TRUE(bool(result));
  EXPECT_EQ(result->error, ECANCELED);
  EXPECT_EQ(f.service.cancel_control_result(observation).error(), EINVAL);
  EXPECT_EQ(f.service.release_cancel(observation), 0);
  EXPECT_EQ(f.service.release_cancel(observation), EINVAL);
  EXPECT_EQ(f.service.retain_cancel(&cb, observation), EINVAL);
  EXPECT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest, SuccessfulControlDoesNotCompleteOriginal) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(3)));
  NotificationState notification;
  int cb = 0;
  Service::CancelObservation observation;
  ASSERT_TRUE(bool(f.admit(&cb, notification)));
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.service.retain_cancel(&cb, observation), 0);
  ASSERT_EQ(f.service.request_cancel(observation), 0);
  EXPECT_EQ(f.service.release_cancel(observation), EBUSY);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.ring.sq[2].opcode, uint8_t(IORING_OP_ASYNC_CANCEL));
  EXPECT_EQ(f.ring.sq[2].addr, f.ring.sq[1].user_data);
  f.ring.complete(2, 0);
  ASSERT_EQ(f.service.submit(), 0);
  auto control = f.service.cancel_control_result(observation);
  ASSERT_TRUE(bool(control));
  EXPECT_EQ(*control, 0);
  EXPECT_EQ(f.service.original_cancel_result(observation).error(), EINPROGRESS);
  EXPECT_EQ(*f.service.error(&cb), EINPROGRESS);
  f.finish(0, -ECANCELED);
  ASSERT_EQ(f.service.submit(), 0);
  auto original = f.service.original_cancel_result(observation);
  ASSERT_TRUE(bool(original));
  EXPECT_EQ(original->error, ECANCELED);
  EXPECT_EQ(f.service.release_cancel(observation), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  EXPECT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest, FailedControlKeepsSnapshotAcrossReuse) {
  Fixture f;
  Fixture other;
  ASSERT_TRUE(bool(f.service.initialize(8)));
  NotificationState old, fresh;
  int cb = 0;
  Service::CancelObservation observation;
  ASSERT_TRUE(bool(f.admit(&cb, old)));
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.service.retain_cancel(&cb, observation), 0);
  EXPECT_EQ(other.service.request_cancel(observation), EINVAL);
  EXPECT_EQ(other.service.release_cancel(observation), EINVAL);
  ASSERT_EQ(f.service.request_cancel(observation), 0);
  // Completion wins before the worker can publish the cancel SQE. The
  // engine drops its cancel reference, but the independent snapshot survives.
  f.finish(0, 6);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_EQ(f.ring.tail, 2U);
  EXPECT_EQ(f.service.cancel_control_result(observation).error(), EALREADY);
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  ASSERT_TRUE(bool(f.admit(&cb, fresh)));
  EXPECT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.service.retained(), size_t(2));
  EXPECT_EQ(*f.service.error(&cb), EINPROGRESS);
  auto original = f.service.original_cancel_result(observation);
  ASSERT_TRUE(bool(original));
  EXPECT_EQ(original->value, ssize_t(6));
  EXPECT_EQ(f.service.release_cancel(observation), 0);
  EXPECT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(old.destroyed, 1U);
  ASSERT_EQ(f.service.submit(), 0);
  f.finish(2, 4);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  EXPECT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest, WithdrawnCancelRetainsOriginalOwnership) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(3)));
  NotificationState notification;
  int cb = 0;
  Service::CancelObservation observation;
  ASSERT_TRUE(bool(f.admit(&cb, notification)));
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.service.retain_cancel(&cb, observation), 0);
  ASSERT_EQ(f.service.request_cancel(observation), 0);
  f.ring.submit_error = EIO;
  EXPECT_EQ(f.service.submit(), EIO);
  EXPECT_EQ(f.ring.tail, 2U);
  EXPECT_EQ(f.service.cancel_control_result(observation).error(), EIO);
  EXPECT_EQ(f.service.original_cancel_result(observation).error(), EINPROGRESS);
  EXPECT_EQ(f.service.release_cancel(observation), 0);
  EXPECT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.resources.closed, 0U);
  f.ring.submit_error = 0;
  f.finish(0);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  EXPECT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest, CancelCreditFailureKeepsIndependentPin) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(3)));
  NotificationState notification;
  int cb = 0;
  Service::CancelObservation first, second;
  ASSERT_TRUE(bool(f.admit(&cb, notification)));
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.service.retain_cancel(&cb, first), 0);
  ASSERT_EQ(f.service.retain_cancel(&cb, second), 0);
  ASSERT_EQ(f.service.request_cancel(first), 0);
  EXPECT_EQ(f.service.request_cancel(second), EAGAIN);
  EXPECT_EQ(f.service.original_cancel_result(second).error(), EINPROGRESS);
  f.finish(0);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  ASSERT_EQ(f.service.release_cancel(first), 0);
  EXPECT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.service.retained(), size_t(1));
  auto result = f.service.original_cancel_result(second);
  ASSERT_TRUE(bool(result));
  EXPECT_EQ(result->value, ssize_t(8));
  EXPECT_EQ(f.service.release_cancel(second), 0);
  EXPECT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest, PreparationRollbackHasIndependentOwners) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(3)));
  NotificationState notification;
  notification.prepare_error = ENOTSUP;
  notification.cleanup_error = EIO;
  int cb = 0;
  f.ring.unregister_error = EBUSY;
  f.resources.close_error = EINTR;
  EXPECT_EQ(f.admit(&cb, notification).error(), ENOTSUP);
  EXPECT_EQ(f.service.complete(&cb).error(), EINVAL);
  EXPECT_EQ(f.service.maintain(), EIO);
  EXPECT_EQ(f.service.maintain(), EIO);
  EXPECT_EQ(f.resources.closed, 1U);
  EXPECT_EQ(f.ring.personalities, 1U);
  EXPECT_EQ(notification.discarded, 1U);
  EXPECT_EQ(notification.activated, 0U);
  EXPECT_EQ(notification.destroyed, 0U);
  notification.cleanup_error = 0;
  notification.callback_resources = true;
  f.ring.unregister_error = 0;
  EXPECT_EQ(f.service.maintain(), EBUSY);
  EXPECT_EQ(f.ring.personalities, 0U);
  EXPECT_EQ(f.service.retained(), size_t(1));
  notification.callback_resources = false;
  EXPECT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(notification.destroyed, 1U);
  EXPECT_EQ(f.resources.closed, 1U);
}

TEST(LlvmLibcAioServiceRegistryTest, DuplicateRevalidatedAfterPreparation) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(8)));
  NotificationState first, second;
  int cb = 0;
  auto result = f.service.admit(input(&cb), [&](Notification &n) {
    EXPECT_EQ(f.service.retained(), size_t(1));
    EXPECT_EQ(f.service.error(&cb).error(), EINVAL);
    EXPECT_EQ(f.service.maintain(), 0); // Preparing record stays pinned.
    EXPECT_TRUE(bool(f.admit(&cb, second)));
    return n.prepare(first);
  });
  EXPECT_EQ(result.error(), EBUSY);
  EXPECT_EQ(f.service.retained(), size_t(2));
  EXPECT_EQ(*f.service.error(&cb), EINPROGRESS);
  EXPECT_EQ(f.ring.tail, 0U);
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(first.discarded, 1U);
  EXPECT_EQ(first.destroyed, 1U);
  EXPECT_EQ(second.discarded, 0U);
  EXPECT_EQ(f.service.retained(), size_t(1));
  ASSERT_EQ(f.service.submit(), 0);
  f.finish(0);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  ASSERT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest, CreditRevalidationRejectsPreparedRecord) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(3)));
  NotificationState first, second;
  int a = 0, b = 0;
  auto result = f.service.admit(input(&a), [&](Notification &n) {
    EXPECT_TRUE(bool(f.admit(&b, second)));
    return n.prepare(first);
  });
  EXPECT_EQ(result.error(), EAGAIN);
  EXPECT_EQ(f.service.error(&a).error(), EINVAL);
  EXPECT_EQ(*f.service.error(&b), EINPROGRESS);
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(first.destroyed, 1U);
  ASSERT_EQ(f.service.submit(), 0);
  f.finish(0);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&b)));
  ASSERT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest,
     ReuseKeepsOldMetadataAndCallbackResources) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(8)));
  NotificationState old, fresh;
  old.callback_resources = true;
  // Deliberately not dereferenceable. The address is a binding key only.
  const void *cb = reinterpret_cast<void *>(1);
  ASSERT_TRUE(bool(f.admit(cb, old)));
  EXPECT_EQ(f.service.take_result(cb).error(), EINPROGRESS);
  EXPECT_FALSE(*f.service.complete(cb));
  ASSERT_EQ(f.service.submit(), 0);
  f.ring.complete(1, -EINVAL);
  ASSERT_EQ(f.service.submit(), 0);
  auto status = f.service.error(cb);
  ASSERT_TRUE(bool(status));
  EXPECT_EQ(*status, EINVAL); // Operation error, not an invalid binding.
  EXPECT_TRUE(*f.service.complete(cb));
  auto result = f.service.take_result(cb);
  ASSERT_TRUE(bool(result));
  EXPECT_EQ(result->error, EINVAL);
  EXPECT_EQ(result->value, ssize_t(-1));
  EXPECT_EQ(f.service.take_result(cb).error(), EINVAL);
  EXPECT_EQ(f.service.error(cb).error(), EINVAL);
  ASSERT_TRUE(bool(f.admit(cb, fresh)));
  EXPECT_EQ(*f.service.error(cb), EINPROGRESS);
  EXPECT_EQ(f.service.maintain(), EBUSY);
  EXPECT_EQ(old.activated, 1U);
  EXPECT_EQ(old.destroyed, 0U);
  EXPECT_EQ(f.resources.closed, 0U);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_NE(f.ring.sq[1].user_data, f.ring.sq[3].user_data);
  f.finish(2, 4);
  ASSERT_EQ(f.service.submit(), 0);
  result = f.service.take_result(cb);
  ASSERT_TRUE(bool(result));
  EXPECT_EQ(result->value, ssize_t(4));
  EXPECT_EQ(f.service.maintain(), EBUSY);
  EXPECT_EQ(fresh.destroyed, 1U);
  old.callback_resources = false;
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(old.destroyed, 0U); // Original metadata still has kernel ownership.
  EXPECT_EQ(f.resources.closed, 1U);
  f.ring.identify(0);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(old.destroyed, 1U);
  EXPECT_EQ(old.activated, 1U);
}

TEST(LlvmLibcAioServiceRegistryTest, NotificationRetryDoesNotChangeResult) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(3)));
  NotificationState notification;
  notification.activate_error = EAGAIN;
  int cb = 0;
  ASSERT_TRUE(bool(f.admit(&cb, notification)));
  ASSERT_EQ(f.service.submit(), 0);
  f.finish(0, 6);
  ASSERT_EQ(f.service.submit(), 0);
  auto result = f.service.take_result(&cb);
  ASSERT_TRUE(bool(result));
  EXPECT_EQ(result->value, ssize_t(6));
  EXPECT_EQ(f.service.maintain(), EAGAIN);
  notification.activate_error = EBUSY;
  EXPECT_EQ(f.service.maintain(), EBUSY);
  EXPECT_EQ(notification.cleaned, 0U);
  EXPECT_EQ(f.resources.closed, 0U);
  notification.activate_error = EIO; // Terminal delivery error, not I/O error.
  EXPECT_EQ(f.service.maintain(), EIO);
  EXPECT_EQ(notification.activated, 3U);
  EXPECT_EQ(notification.destroyed, 1U);
}

TEST(LlvmLibcAioServiceRegistryTest, QueriesInsideSubmitAndFailedRelease) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(3)));
  NotificationState notification;
  int cb = 0;
  ASSERT_TRUE(bool(f.admit(&cb, notification)));
  struct Probe {
    Service *service;
    const void *cb;
    bool returned = false;
    unsigned calls = 0;
    static void check(void *context) {
      auto &p = *static_cast<Probe *>(context);
      ++p.calls;
      EXPECT_EQ(p.service->retained(), size_t(1));
      if (p.returned) {
        EXPECT_EQ(p.service->error(p.cb).error(), EINVAL);
        EXPECT_EQ(p.service->take_result(p.cb).error(), EINVAL);
      } else {
        EXPECT_EQ(*p.service->error(p.cb), EINPROGRESS);
        EXPECT_EQ(p.service->take_result(p.cb).error(), EINPROGRESS);
      }
    }
  } probe{&f.service, &cb};
  f.ring.on_submit = Probe::check;
  f.ring.context = &probe;
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_EQ(probe.calls, 1U);
  f.ring.on_submit = nullptr;
  f.finish(0);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  probe.returned = true;
  f.resources.on_close = Probe::check;
  f.resources.context = &probe;
  f.ring.unregister_error = EBUSY;
  EXPECT_EQ(f.service.maintain(), EBUSY);
  EXPECT_EQ(f.service.maintain(), EBUSY);
  EXPECT_EQ(probe.calls, 2U);
  EXPECT_EQ(f.resources.closed, 1U);
  EXPECT_EQ(f.service.retained(), size_t(1));
  EXPECT_EQ(notification.destroyed, 0U);
  f.ring.unregister_error = 0;
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(notification.destroyed, 1U);
}

TEST(LlvmLibcAioServiceRegistryTest, FailedSubmitUsesSplitWithdrawal) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(3)));
  NotificationState notification;
  int cb = 0;
  ASSERT_TRUE(bool(f.admit(&cb, notification)));
  f.ring.submit_error = EIO;
  EXPECT_EQ(f.service.submit(), EIO);
  EXPECT_EQ(f.ring.tail, 0U);
  EXPECT_EQ(*f.service.error(&cb), EIO);
  auto result = f.service.take_result(&cb);
  ASSERT_TRUE(bool(result));
  EXPECT_EQ(result->error, EIO);
  EXPECT_EQ(f.resources.closed, 0U);
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(notification.activated, 1U);
}

TEST(LlvmLibcAioServiceRegistryTest, CleanupReentersAdmissionWithoutRingLock) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(3)));
  NotificationState first, second;
  int cb = 0;
  ASSERT_TRUE(bool(f.admit(&cb, first)));
  ASSERT_EQ(f.service.submit(), 0);
  f.finish(0);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb)));

  struct Reenter {
    Fixture *fixture;
    NotificationState *notification;
    const void *cb;
    unsigned calls = 0;
    static void cleanup(void *context) {
      auto &self = *static_cast<Reenter *>(context);
      ++self.calls;
      EXPECT_FALSE(CheckedGate::held);
      // A loader-side callback release can meet a constructor performing
      // admission. This scripted reentry needs capture's ring-operation lock.
      EXPECT_TRUE(bool(self.fixture->admit(self.cb, *self.notification)));
      EXPECT_EQ(self.fixture->service.maintain(), EBUSY);
      EXPECT_EQ(*self.fixture->service.error(self.cb), EINPROGRESS);
    }
  } reenter{&f, &second, &cb};
  first.on_cleanup = Reenter::cleanup;
  first.context = &reenter;
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(reenter.calls, 1U);
  EXPECT_EQ(first.destroyed, 1U);
  EXPECT_EQ(second.cleaned, 0U); // Concurrent prepended head waits one pass.
  EXPECT_EQ(f.service.retained(), size_t(1));
  EXPECT_EQ(*f.service.error(&cb), EINPROGRESS);
  ASSERT_EQ(f.service.submit(), 0);
  f.finish(2);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(second.destroyed, 1U);
}

TEST(LlvmLibcAioServiceRegistryTest, OriginalCompletionLatchesServiceWait) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(3)));
  NotificationState notification;
  int cb = 0;
  ASSERT_TRUE(bool(f.admit(&cb, notification)));
  LIBC_NAMESPACE::mmix::JumpCleanupStack::Node *head = nullptr;
  auto wait = f.service.make_wait(head);
  const void *list[] = {&cb, nullptr, &cb};
  ASSERT_EQ(f.service.begin_wait(wait, list, 3), 0);
  ASSERT_EQ(f.service.submit(), 0);
  f.ring.identify(0);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_FALSE(wait.completion().ready());
  f.ring.complete(1, 1);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_TRUE(wait.completion().ready());
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.service.retained(), size_t(0));
  // The latched waiter neither owns nor rereads the retired result record.
  EXPECT_EQ(wait.completion().wait(), 0);
  EXPECT_EQ(wait.finish(), 0);
  EXPECT_EQ(head, nullptr);
}

TEST(LlvmLibcAioServiceRegistryTest, WaitRejectsForeignOwnerAndInvalidBinding) {
  Fixture first, second;
  ASSERT_TRUE(bool(first.service.initialize(3)));
  ASSERT_TRUE(bool(second.service.initialize(3)));
  LIBC_NAMESPACE::mmix::JumpCleanupStack::Node *head = nullptr;
  auto wait = first.service.make_wait(head);
  int cb = 0;
  const void *list[] = {&cb};
  EXPECT_EQ(second.service.begin_wait(wait, list, 1), EINVAL);
  EXPECT_EQ(head, nullptr);
  EXPECT_EQ(first.service.begin_wait(wait, list, 1), EINVAL);
  EXPECT_EQ(head, nullptr);
  EXPECT_FALSE(wait.completion().ready());
}

TEST(LlvmLibcAioServiceRegistryTest, WaitAfterCompletionAndBeforeAddressReuse) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(3)));
  NotificationState first, second;
  int cb = 0;
  ASSERT_TRUE(bool(f.admit(&cb, first)));
  ASSERT_EQ(f.service.submit(), 0);
  f.finish(0);
  ASSERT_EQ(f.service.submit(), 0);
  LIBC_NAMESPACE::mmix::JumpCleanupStack::Node *head = nullptr;
  auto wait = f.service.make_wait(head);
  const void *list[] = {&cb};
  ASSERT_EQ(f.service.begin_wait(wait, list, 1), 0);
  EXPECT_TRUE(wait.completion().ready());
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  ASSERT_EQ(f.service.maintain(), 0);
  ASSERT_TRUE(bool(f.admit(&cb, second)));
  EXPECT_EQ(*f.service.error(&cb), EINPROGRESS);
  EXPECT_EQ(wait.completion().wait(), 0);
  ASSERT_EQ(wait.finish(), 0);
  ASSERT_EQ(f.service.submit(), 0);
  f.finish(2);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  ASSERT_EQ(f.service.maintain(), 0);
}
