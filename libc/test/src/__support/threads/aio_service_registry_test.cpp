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
};

struct ResourceState {
  unsigned closed = 0, duplicated = 0;
  int next_descriptor = 100;
  int duplicate_error = 0, close_error = 0;
  void (*on_close)(void *) = nullptr;
  void *context = nullptr;
};
struct ResourceOps {
  ResourceState *state;
  ErrorOr<int> duplicate(int) {
    EXPECT_FALSE(CheckedGate::held);
    ++state->duplicated;
    if (state->duplicate_error)
      return Error(state->duplicate_error);
    return ++state->next_descriptor;
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
  unsigned fail_after = UINT_MAX;
  unsigned allocated = 0, freed = 0;
  void (*on_allocate)(void *) = nullptr;
  void *context = nullptr;
  void *allocate(size_t size, size_t alignment) {
    EXPECT_FALSE(CheckedGate::held);
    if (on_allocate) {
      auto callback = on_allocate;
      on_allocate = nullptr;
      callback(context);
    }
    if (fail || allocated == fail_after)
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
Service::Input input(const void *address, Op op = Op::Read, int fd = 7) {
  return {address, fd, op, nullptr, 8, 42, false};
}
struct Fixture {
  FakeRing ring;
  Allocator allocator;
  ResourceState resources;
  uint64_t watermark = 0;
  Service service{ring, allocator, {&resources}, watermark, 17};

  ErrorOr<void> admit(const void *cb, NotificationState &notification,
                      Op op = Op::Read, int fd = 7) {
    return service.admit(input(cb, op, fd), [&](Notification &n) {
      return n.prepare(notification);
    });
  }
  void finish(unsigned index, int result = 8) { ring.complete(index, result); }
  ~Fixture() {
    EXPECT_FALSE(service.has_cancel_batches());
    EXPECT_FALSE(service.has_list_groups());
    EXPECT_EQ(service.retained(), size_t(0));
    EXPECT_EQ(ring.personalities, 0U);
    EXPECT_EQ(allocator.allocated, allocator.freed);
  }
};
} // namespace

TEST(LlvmLibcAioServiceRegistryTest,
     NormalizeCapturedDescriptorBeforePrepareOutsideBothLocks) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(4)));
  int cb[2]{}, buffer{};
  NotificationState notification, nested;
  bool normalized = false;
  int retained_fd = -1;
  auto admitted = f.service.admit(
      input(&cb[0], Op::Write),
      [&](Notification &n) {
        EXPECT_TRUE(normalized);
        return n.prepare(notification);
      },
      [&](int descriptor, Service::Input &value) {
        EXPECT_FALSE(CheckedGate::held);
        EXPECT_EQ(f.resources.duplicated, 1U);
        EXPECT_EQ(f.ring.personalities, 1U);
        EXPECT_EQ(notification.prepared, 0U);
        EXPECT_EQ(descriptor, f.resources.next_descriptor);
        EXPECT_NE(descriptor, value.descriptor);
        EXPECT_EQ(value.descriptor, 7);
        retained_fd = descriptor;
        EXPECT_EQ(f.service.error(&cb[0]).error(), EINVAL);
        // Maintenance must leave this Preparing record pinned. Nested capture
        // takes ring_operations, proving the hook does not hold that lock.
        EXPECT_EQ(f.service.maintain(), 0);
        EXPECT_TRUE(bool(f.admit(&cb[1], nested, Op::Read, 8)));
        value.buffer = &buffer;
        value.size = 4;
        value.offset = 123;
        value.transfer_flags = RWF_APPEND;
        normalized = true;
        return 0;
      });
  ASSERT_TRUE(bool(admitted));
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.ring.tail, 2U);
  EXPECT_EQ(f.ring.sq[1].fd, retained_fd);
  EXPECT_EQ(uint64_t(f.ring.sq[1].addr), uint64_t(uintptr_t(&buffer)));
  EXPECT_EQ(f.ring.sq[1].len, 4U);
  EXPECT_EQ(uint64_t(f.ring.sq[1].off), uint64_t(123));
  EXPECT_EQ(unsigned(f.ring.sq[1].rw_flags), unsigned(RWF_APPEND));
  EXPECT_EQ(*f.service.cancel_count(7), size_t(1));
  EXPECT_EQ(*f.service.cancel_count(retained_fd), size_t(0));
  f.finish(0);
  f.finish(1, 4);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_EQ(f.service.take_result(&cb[0])->value, ptrdiff_t(4));
  ASSERT_TRUE(bool(f.service.take_result(&cb[1])));
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(notification.activated, 1U);
}

TEST(LlvmLibcAioServiceRegistryTest, FailedCaptureSkipsNormalization) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  int cb{};
  f.resources.duplicate_error = EMFILE;
  auto admitted = f.service.admit(
      input(&cb),
      [](Notification &) {
        EXPECT_TRUE(false);
        return 0;
      },
      [](int, Service::Input &) {
        EXPECT_TRUE(false);
        return 0;
      });
  EXPECT_EQ(admitted.error(), EMFILE);
  EXPECT_EQ(f.service.error(&cb).error(), EINVAL);
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.resources.closed, 0U);
}

TEST(LlvmLibcAioServiceRegistryTest,
     NormalizationFailureRetainsRollbackUntilUnregisterSucceeds) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  int cb{};
  NotificationState notification;
  f.ring.unregister_error = EBUSY;
  auto admitted = f.service.admit(
      input(&cb), [&](Notification &n) { return n.prepare(notification); },
      [&](int descriptor, Service::Input &value) {
        EXPECT_FALSE(CheckedGate::held);
        EXPECT_EQ(descriptor, f.resources.next_descriptor);
        value.size = 0;
        return EOVERFLOW;
      });
  EXPECT_EQ(admitted.error(), EOVERFLOW);
  EXPECT_EQ(notification.prepared, 0U);
  EXPECT_EQ(f.service.error(&cb).error(), EINVAL);
  EXPECT_EQ(f.service.retained(), size_t(1));
  EXPECT_EQ(f.ring.unregisters, 0U);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_EQ(f.ring.tail, 0U);
  EXPECT_EQ(f.service.maintain(), EBUSY);
  EXPECT_EQ(f.service.maintain(), EBUSY);
  EXPECT_EQ(f.resources.closed, 1U);
  EXPECT_EQ(f.ring.unregisters, 2U);
  EXPECT_EQ(f.ring.personalities, 1U);
  EXPECT_EQ(f.service.retained(), size_t(1));
  f.ring.unregister_error = 0;
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.resources.closed, 1U);
  EXPECT_EQ(f.ring.unregisters, 3U);
  EXPECT_EQ(notification.activated, 0U);
}

TEST(LlvmLibcAioServiceRegistryTest,
     PreparedNormalizationFailureRetainsResultAndCleanup) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  int cb{};
  Service::PreparedMember member;
  Service::MemberObservation observation;
  NotificationState notification;
  ASSERT_TRUE(bool(f.service.prepare_member(member, input(&cb))));
  EXPECT_EQ(f.resources.duplicated, 0U);
  auto started = f.service.start_member(
      member, [&](Notification &n) { return n.prepare(notification); },
      &observation,
      [&](int descriptor, Service::Input &) {
        EXPECT_FALSE(CheckedGate::held);
        EXPECT_EQ(descriptor, f.resources.next_descriptor);
        EXPECT_EQ(f.service.error(&cb).error(), EINVAL);
        return EOVERFLOW;
      });
  EXPECT_EQ(started.error(), EOVERFLOW);
  EXPECT_EQ(notification.prepared, 0U);
  EXPECT_EQ(*f.service.error(&cb), EOVERFLOW);
  auto result = f.service.take_result(&cb);
  ASSERT_TRUE(bool(result));
  EXPECT_EQ(result->error, EOVERFLOW);
  EXPECT_EQ(result->value, ptrdiff_t(-1));
  EXPECT_EQ(f.service.member_result(observation)->error, EOVERFLOW);
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.resources.closed, 0U); // Observation still pins the request.
  ASSERT_EQ(f.service.release_member(observation), 0);
  f.ring.unregister_error = EBUSY;
  EXPECT_EQ(f.service.maintain(), EBUSY);
  EXPECT_EQ(f.resources.closed, 1U);
  EXPECT_EQ(f.service.retained(), size_t(1));
  f.ring.unregister_error = 0;
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.resources.closed, 1U);
  EXPECT_EQ(f.ring.tail, 0U);
}

TEST(LlvmLibcAioServiceRegistryTest,
     ListNormalizationKeepsPartialResultsAndSkipsFailedMemberPrepare) {
  const int failures[] = {EINVAL, ENOMEM};
  for (int failure : failures) {
    Fixture f;
    ASSERT_TRUE(bool(f.service.initialize(4)));
    int cb[3]{}, buffer{};
    Service::ListInput inputs[] = {
        {input(&cb[0])}, {input(&cb[1])}, {input(&cb[2]), EBADF}};
    NotificationState group, members[3];
    auto list = f.service.prepare_list(
        inputs, 3, [&](Notification &n) { return n.prepare(group); });
    ASSERT_TRUE(bool(list));
    unsigned normalized = 0;
    int retained_fd = -1;
    EXPECT_EQ(f.service.start_list(
                  **list,
                  [&](size_t i, Notification &n) {
                    EXPECT_EQ(i, size_t(0));
                    EXPECT_EQ(normalized, 1U);
                    return n.prepare(members[i]);
                  },
                  [&](size_t i, int descriptor, Service::Input &value) {
                    EXPECT_FALSE(CheckedGate::held);
                    EXPECT_EQ(i, size_t(normalized));
                    ++normalized;
                    EXPECT_EQ(f.resources.duplicated, normalized);
                    EXPECT_EQ(descriptor, f.resources.next_descriptor);
                    EXPECT_EQ(members[i].prepared, 0U);
                    EXPECT_EQ(f.service.error(&cb[i]).error(), EINVAL);
                    EXPECT_EQ(f.service.maintain(), 0);
                    if (i == 1)
                      return failure;
                    retained_fd = descriptor;
                    value.buffer = &buffer;
                    value.size = 4;
                    value.offset = -1;
                    return 0;
                  }),
              failure == ENOMEM ? EAGAIN : EIO);
    EXPECT_EQ(normalized, 2U);
    EXPECT_EQ(members[1].prepared, 0U);
    EXPECT_EQ(members[2].prepared, 0U);
    EXPECT_EQ(*f.service.error(&cb[0]), EINPROGRESS);
    EXPECT_EQ(*f.service.error(&cb[1]), failure);
    EXPECT_EQ(*f.service.error(&cb[2]), EBADF);
    EXPECT_EQ(f.service.list_result(**list).error(), EINPROGRESS);
    ASSERT_EQ(f.service.submit(), 0);
    ASSERT_EQ(f.ring.tail, 1U);
    EXPECT_EQ(f.ring.sq[0].fd, retained_fd);
    EXPECT_EQ(uint64_t(f.ring.sq[0].addr), uint64_t(uintptr_t(&buffer)));
    EXPECT_EQ(f.ring.sq[0].len, 4U);
    EXPECT_EQ(uint64_t(f.ring.sq[0].off), UINT64_MAX);
    f.finish(0, 4);
    ASSERT_EQ(f.service.submit(), 0);
    EXPECT_EQ(*f.service.list_result(**list), failure == ENOMEM ? EAGAIN : EIO);
    for (auto &key : cb)
      ASSERT_TRUE(bool(f.service.take_result(&key)));
    ASSERT_EQ(f.service.finish_list(**list), 0);
    ASSERT_EQ(f.service.maintain(), 0);
    EXPECT_EQ(group.activated, 1U);
    EXPECT_EQ(members[0].activated, 1U);
    EXPECT_EQ(members[1].activated, 0U);
    EXPECT_EQ(f.resources.closed, 2U);
  }
}

TEST(LlvmLibcAioServiceRegistryTest,
     NormalizationCannotChangeIdentityOperationOrNotificationPolicy) {
  const bool retain_modes[] = {false, true};
  const int failures[] = {0, EIO};
  for (bool retain : retain_modes) {
    for (unsigned field = 0; field < 5; ++field) {
      for (int failure : failures) {
        Fixture f;
        ASSERT_TRUE(bool(f.service.initialize(2)));
        int cb[2]{};
        NotificationState notification;
        auto prepare = [&](Notification &n) { return n.prepare(notification); };
        auto normalize = [&](int, Service::Input &value) {
          switch (field) {
          case 0:
            value.control_block = &cb[1];
            break;
          case 1:
            value.descriptor = 8;
            break;
          case 2:
            value.operation = Op::Write;
            break;
          case 3:
            value.notify = true;
            break;
          case 4:
            value.transfer_flags = RWF_NOWAIT;
            break;
          }
          return failure;
        };
        if (retain) {
          Service::PreparedMember member;
          ASSERT_TRUE(bool(f.service.prepare_member(member, input(&cb[0]))));
          EXPECT_EQ(f.service.start_member(member, prepare, nullptr, normalize)
                        .error(),
                    EINVAL);
          EXPECT_EQ(*f.service.error(&cb[0]), EINVAL);
          auto result = f.service.take_result(&cb[0]);
          ASSERT_TRUE(bool(result));
          EXPECT_EQ(result->error, EINVAL);
        } else {
          EXPECT_EQ(f.service.admit(input(&cb[0]), prepare, normalize).error(),
                    EINVAL);
          EXPECT_EQ(f.service.error(&cb[0]).error(), EINVAL);
        }
        EXPECT_EQ(f.service.error(&cb[1]).error(), EINVAL);
        EXPECT_EQ(notification.prepared, 0U);
        EXPECT_EQ(f.ring.tail, 0U);
        ASSERT_EQ(f.service.maintain(), 0);
        EXPECT_EQ(f.resources.closed, 1U);
      }
    }
  }
}

TEST(LlvmLibcAioServiceRegistryTest, InvalidNormalizationStatusIsRejected) {
  const int failures[] = {-EIO, EINPROGRESS};
  for (int failure : failures) {
    Fixture f;
    ASSERT_TRUE(bool(f.service.initialize(2)));
    int cb{};
    Service::PreparedMember member;
    ASSERT_TRUE(bool(f.service.prepare_member(member, input(&cb))));
    EXPECT_EQ(f.service
                  .start_member(
                      member,
                      [](Notification &) {
                        EXPECT_TRUE(false);
                        return 0;
                      },
                      nullptr, [&](int, Service::Input &) { return failure; })
                  .error(),
              EINVAL);
    EXPECT_EQ(f.service.take_result(&cb)->error, EINVAL);
    ASSERT_EQ(f.service.maintain(), 0);
  }
}

TEST(LlvmLibcAioServiceRegistryTest, ListPreparationFailureStartsNothing) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(4)));
  int cb[3]{};
  Service::ListInput inputs[] = {
      {input(&cb[0])}, {input(&cb[1])}, {input(&cb[2])}};
  NotificationState group;
  // Group storage plus the first two-member block; fail the next block.
  f.allocator.fail_after = 2;
  auto list = f.service.prepare_list(
      inputs, 3, [&](Notification &n) { return n.prepare(group); });
  EXPECT_FALSE(bool(list));
  EXPECT_EQ(list.error(), ENOMEM);
  EXPECT_EQ(group.prepared, 0U);
  EXPECT_EQ(f.resources.duplicated, 0U);
  EXPECT_EQ(f.ring.tail, 0U);
  for (auto &key : cb)
    EXPECT_EQ(f.service.error(&key).error(), EINVAL);
  EXPECT_TRUE(f.service.has_list_groups());
  ASSERT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest, ListGroupNotificationFailureRollsBack) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(3)));
  int cb[2]{};
  Service::ListInput inputs[] = {{input(&cb[0])}, {input(&cb[1])}};
  NotificationState group;
  group.prepare_error = EAGAIN;
  group.cleanup_error = EIO;
  auto list = f.service.prepare_list(
      inputs, 2, [&](Notification &n) { return n.prepare(group); });
  EXPECT_EQ(list.error(), EAGAIN);
  EXPECT_EQ(f.resources.duplicated, 0U);
  EXPECT_EQ(f.ring.tail, 0U);
  ASSERT_EQ(f.service.maintain(), EIO);
  EXPECT_TRUE(f.service.has_list_groups());
  EXPECT_EQ(group.activated, 0U);
  group.cleanup_error = 0;
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(group.discarded, 1U);
  EXPECT_EQ(group.destroyed, 1U);
}

TEST(LlvmLibcAioServiceRegistryTest, ListPartialFailureRetainsAllResults) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  int cb[3]{};
  Service::ListInput inputs[] = {
      {input(&cb[0])}, {input(&cb[1], Op::Read, -1), EINVAL}, {input(&cb[2])}};
  NotificationState group, members[3];
  auto list = f.service.prepare_list(
      inputs, 3, [&](Notification &n) { return n.prepare(group); });
  ASSERT_TRUE(bool(list));
  f.allocator.fail = true; // No post-acceptance allocation for local errors.
  EXPECT_EQ(f.service.start_list(**list,
                                 [&](size_t i, Notification &n) {
                                   return n.prepare(members[i]);
                                 }),
            EAGAIN);
  EXPECT_EQ(*f.service.error(&cb[0]), EINPROGRESS);
  EXPECT_EQ(*f.service.error(&cb[1]), EINVAL);
  EXPECT_EQ(*f.service.error(&cb[2]), EAGAIN);
  EXPECT_EQ(members[1].prepared, 0U);
  EXPECT_EQ(f.resources.duplicated, 2U);
  EXPECT_EQ(f.service.list_result(**list).error(), EINPROGRESS);
  EXPECT_EQ(f.service.take_result(&cb[1])->error, EINVAL);
  EXPECT_EQ(f.service.take_result(&cb[2])->error, EAGAIN);
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(group.activated, 0U);
  ASSERT_EQ(f.service.submit(), 0);
  f.finish(0);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_EQ(*f.service.list_result(**list), EAGAIN);
  ASSERT_TRUE(bool(f.service.take_result(&cb[0])));
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(group.activated, 1U);
  EXPECT_EQ(members[0].activated, 1U);
  EXPECT_EQ(members[2].activated, 0U);
  EXPECT_TRUE(f.service.has_list_groups()); // Caller still holds status.
  EXPECT_EQ(f.service.retained(), size_t(0));
  EXPECT_EQ(*f.service.list_result(**list), EAGAIN);
  ASSERT_EQ(f.service.finish_list(**list), 0);
  ASSERT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest, ReleasedListWaitsForAllOriginalResults) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(4)));
  int cb[2]{};
  Service::ListInput inputs[] = {{input(&cb[0])}, {input(&cb[1])}};
  NotificationState group, members[3];
  auto list = f.service.prepare_list(
      inputs, 2, [&](Notification &n) { return n.prepare(group); });
  ASSERT_TRUE(bool(list));
  ASSERT_EQ(f.service.start_list(**list,
                                 [&](size_t i, Notification &n) {
                                   return n.prepare(members[i]);
                                 }),
            0);
  ASSERT_EQ(f.service.submit(), 0);
  f.finish(0, -EIO);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb[0])));
  ASSERT_TRUE(bool(f.admit(&cb[0], members[2]))); // New binding stays pending.
  EXPECT_EQ(f.service.list_result(**list).error(), EINPROGRESS);
  ASSERT_EQ(f.service.finish_list(**list), 0);
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(group.activated, 0U);
  EXPECT_TRUE(f.service.has_list_groups());
  f.finish(1);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb[1])));
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_FALSE(f.service.has_list_groups());
  EXPECT_EQ(group.activated, 1U);
  EXPECT_EQ(*f.service.error(&cb[0]), EINPROGRESS);
  f.finish(2);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb[0])));
  ASSERT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest, ListCompletionSurvivesNotificationRetry) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  NotificationState group;
  group.activate_error = EAGAIN;
  auto list = f.service.prepare_list(
      nullptr, 0, [&](Notification &n) { return n.prepare(group); });
  ASSERT_TRUE(bool(list));
  ASSERT_EQ(f.service.start_list(**list,
                                 [](size_t, Notification &) {
                                   EXPECT_TRUE(false);
                                   return EINVAL;
                                 }),
            0);
  EXPECT_EQ(*f.service.list_result(**list), 0);
  ASSERT_EQ(f.service.maintain(), EAGAIN);
  EXPECT_EQ(*f.service.list_result(**list), 0);
  ASSERT_EQ(f.service.finish_list(**list), 0);
  group.activate_error = 0;
  group.callback_resources = true;
  ASSERT_EQ(f.service.maintain(), EBUSY);
  EXPECT_TRUE(f.service.has_list_groups());
  group.callback_resources = false;
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(group.activated, 2U);
  EXPECT_EQ(group.destroyed, 1U);
}

TEST(LlvmLibcAioServiceRegistryTest, ListValidationPrecedesResourceOwnership) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  int cb{};
  Service::ListInput duplicate[] = {{input(&cb)}, {input(&cb)}};
  auto prepare = [](Notification &) {
    EXPECT_TRUE(false);
    return EINVAL;
  };
  EXPECT_EQ(f.service.prepare_list(duplicate, 2, prepare).error(), EINVAL);
  EXPECT_EQ(f.service.prepare_list(nullptr, 1, prepare).error(), EINVAL);
  EXPECT_EQ(f.service.prepare_list(duplicate, SIZE_MAX, prepare).error(),
            ENOMEM);
  EXPECT_EQ(f.allocator.allocated, 0U);
}

TEST(LlvmLibcAioServiceRegistryTest, ListWaitReleasesGateAndLatchesCompletion) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  int cb = 0;
  Service::ListInput inputs[] = {{input(&cb)}};
  NotificationState group, member;
  group.activate_error = EAGAIN;
  auto list = f.service.prepare_list(
      inputs, 1, [&](Notification &n) { return n.prepare(group); });
  ASSERT_TRUE(bool(list));
  ASSERT_EQ(
      f.service.start_list(
          **list, [&](size_t, Notification &n) { return n.prepare(member); }),
      0);
  ASSERT_EQ(f.service.submit(), 0);
  using LIBC_NAMESPACE::Futex;
  using LIBC_NAMESPACE::FutexWordType;
  auto interrupt = [](void *, Futex *, int, FutexWordType, const void *,
                      unsigned) {
    EXPECT_FALSE(CheckedGate::held);
    return -EINTR;
  };
  EXPECT_EQ(f.service.wait_list(**list, nullptr, interrupt), EINTR);
  EXPECT_TRUE(f.service.has_list_groups());
  EXPECT_EQ(f.service.list_result(**list).error(), EINPROGRESS);
  auto complete = [](void *context, Futex *, int operation,
                     FutexWordType expected, const void *timeout,
                     unsigned bitset) {
    EXPECT_FALSE(CheckedGate::held);
    EXPECT_EQ(operation, FUTEX_WAIT_BITSET_PRIVATE);
    EXPECT_EQ(expected, FutexWordType(0));
    EXPECT_EQ(timeout, nullptr);
    EXPECT_EQ(bitset, unsigned(FUTEX_BITSET_MATCH_ANY));
    auto &f = *static_cast<Fixture *>(context);
    f.finish(0, -EIO);
    EXPECT_EQ(f.service.submit(), 0);
    EXPECT_EQ(f.service.maintain(), EAGAIN);
    return -EINTR; // Observed original completion wins over interruption.
  };
  EXPECT_EQ(f.service.wait_list(**list, nullptr, complete, &f), 0);
  EXPECT_EQ(*f.service.list_result(**list), EIO);
  EXPECT_EQ(f.service.wait_list(**list, nullptr, interrupt), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  ASSERT_EQ(f.service.finish_list(**list), 0);
  group.activate_error = 0;
  ASSERT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest, ListWakeDoesNotRequireMaintenance) {
  const bool modes[] = {false, true};
  for (bool fail_submit : modes) {
    Fixture f;
    ASSERT_TRUE(bool(f.service.initialize(2)));
    int cb = 0;
    Service::ListInput inputs[] = {{input(&cb)}};
    NotificationState group, member;
    auto list = f.service.prepare_list(
        inputs, 1, [&](Notification &n) { return n.prepare(group); });
    ASSERT_TRUE(bool(list));
    ASSERT_EQ(
        f.service.start_list(
            **list, [&](size_t, Notification &n) { return n.prepare(member); }),
        0);
    if (!fail_submit)
      ASSERT_EQ(f.service.submit(), 0);
    struct Completion {
      Fixture *fixture;
      bool fail_submit;
      unsigned calls = 0;
    } completion{&f, fail_submit};
    auto progress = [](void *context, LIBC_NAMESPACE::Futex *, int,
                       LIBC_NAMESPACE::FutexWordType, const void *, unsigned) {
      auto &state = *static_cast<Completion *>(context);
      auto &f = *state.fixture;
      ++state.calls;
      EXPECT_FALSE(CheckedGate::held);
      if (state.fail_submit)
        f.ring.submit_error = EIO;
      else
        f.finish(0, -EIO);
      EXPECT_EQ(f.service.submit(), state.fail_submit ? EIO : 0);
      // No maintenance or list_result query may supply the missing wakeup.
      return -EINTR;
    };
    EXPECT_EQ(f.service.wait_list(**list, nullptr, progress, &completion), 0);
    EXPECT_EQ(completion.calls, 1U);
    EXPECT_EQ(group.activated, 0U);
    EXPECT_EQ(member.cleaned, 0U);
    EXPECT_EQ(*f.service.list_result(**list), EIO);
    ASSERT_TRUE(bool(f.service.take_result(&cb)));
    ASSERT_EQ(f.service.finish_list(**list), 0);
    ASSERT_EQ(f.service.maintain(), 0);
  }
}

TEST(LlvmLibcAioServiceRegistryTest,
     RejectedListMembersRetainOnlyResultOwnership) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  int cb[4]{};
  NotificationState notification;
  ASSERT_TRUE(bool(f.admit(&cb[0], notification)));
  ASSERT_TRUE(
      bool(f.service.admit_rejected(input(&cb[1], Op::Read, -1), EBADF)));
  ASSERT_TRUE(bool(f.service.admit_rejected(input(&cb[2]), EAGAIN)));
  EXPECT_EQ(f.ring.personalities, 1U);
  EXPECT_EQ(f.ring.tail, 0U);
  EXPECT_EQ(*f.service.error(&cb[0]), EINPROGRESS);
  EXPECT_EQ(*f.service.error(&cb[1]), EBADF);
  EXPECT_EQ(*f.service.error(&cb[2]), EAGAIN);
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.service.retained(), size_t(3));
  auto result = f.service.take_result(&cb[1]);
  ASSERT_TRUE(bool(result));
  EXPECT_EQ(result->error, EBADF);
  EXPECT_EQ(result->value, ssize_t(-1));
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.service.retained(), size_t(2));
  EXPECT_EQ(f.resources.closed, 0U);
  ASSERT_TRUE(bool(f.service.take_result(&cb[2])));
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.ring.tail, 1U);
  f.finish(0);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb[0])));
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(notification.activated, 1U);
  EXPECT_EQ(f.resources.closed, 1U);
}

TEST(LlvmLibcAioServiceRegistryTest,
     PreparedMembersSurvivePartialCreditFailure) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  int cb[2]{};
  Service::PreparedMember members[2];
  NotificationState notifications[2];
  for (unsigned i = 0; i < 2; ++i)
    ASSERT_TRUE(bool(f.service.prepare_member(members[i], input(&cb[i]))));
  EXPECT_EQ(f.service.retained(), size_t(2));
  EXPECT_EQ(f.resources.duplicated, 0U);
  EXPECT_EQ(f.service.error(&cb[0]).error(), EINVAL);
  ASSERT_EQ(f.service.maintain(), 0);
  f.allocator.fail = true;
  ASSERT_TRUE(bool(f.service.start_member(members[0], [&](Notification &n) {
    return n.prepare(notifications[0]);
  })));
  EXPECT_EQ(f.service
                .start_member(members[1],
                              [&](Notification &n) {
                                return n.prepare(notifications[1]);
                              })
                .error(),
            EAGAIN);
  EXPECT_EQ(*f.service.error(&cb[0]), EINPROGRESS);
  EXPECT_EQ(*f.service.error(&cb[1]), EAGAIN);
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(notifications[1].discarded, 1U);
  EXPECT_EQ(notifications[1].activated, 0U);
  EXPECT_EQ(f.service.take_result(&cb[1])->error, EAGAIN);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_EQ(f.ring.tail, 1U);
  f.finish(0);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_EQ(f.service.take_result(&cb[0])->value, ptrdiff_t(8));
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(notifications[0].activated, 1U);
}

TEST(LlvmLibcAioServiceRegistryTest, PreparationFailureStartsNoMembers) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(4)));
  int cb[3]{};
  Service::PreparedMember members[3];
  ASSERT_TRUE(bool(f.service.prepare_member(members[0], input(&cb[0]))));
  ASSERT_TRUE(bool(f.service.prepare_member(members[1], input(&cb[1]))));
  f.allocator.fail = true;
  EXPECT_EQ(f.service.prepare_member(members[2], input(&cb[2])).error(),
            ENOMEM);
  EXPECT_EQ(f.resources.duplicated, 0U);
  EXPECT_EQ(f.ring.tail, 0U);
  EXPECT_EQ(f.service.error(&cb[0]).error(), EINVAL);
  ASSERT_EQ(f.service.abandon_member(members[0]), 0);
  ASSERT_EQ(f.service.abandon_member(members[1]), 0);
  EXPECT_EQ(f.service.abandon_member(members[0]), EINVAL);
  ASSERT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest, MemberObservationSurvivesResultAndReuse) {
  Fixture f, other;
  ASSERT_TRUE(bool(f.service.initialize(3)));
  ASSERT_TRUE(bool(other.service.initialize(2)));
  int cb{};
  Service::PreparedMember member;
  Service::MemberObservation observation;
  NotificationState first, second;
  ASSERT_TRUE(bool(f.service.prepare_member(member, input(&cb))));
  ASSERT_TRUE(bool(f.service.start_member(
      member, [&](Notification &n) { return n.prepare(first); },
      &observation)));
  EXPECT_EQ(f.service.member_result(observation).error(), EINPROGRESS);
  EXPECT_EQ(other.service.member_result(observation).error(), EINVAL);
  EXPECT_EQ(other.service.release_member(observation), EINVAL);
  ASSERT_EQ(f.service.submit(), 0);
  f.finish(0, 3);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_EQ(f.service.take_result(&cb)->value, ptrdiff_t(3));
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.service.retained(), size_t(1));
  ASSERT_TRUE(bool(f.service.prepare_member(member, input(&cb))));
  EXPECT_EQ(f.service
                .start_member(
                    member, [&](Notification &n) { return n.prepare(second); },
                    &observation)
                .error(),
            EINVAL);
  EXPECT_EQ(second.prepared, 0U);
  ASSERT_TRUE(bool(f.service.start_member(
      member, [&](Notification &n) { return n.prepare(second); })));
  EXPECT_EQ(*f.service.error(&cb), EINPROGRESS);
  EXPECT_EQ(f.service.member_result(observation)->value, ptrdiff_t(3));
  ASSERT_EQ(f.service.release_member(observation), 0);
  EXPECT_EQ(f.service.release_member(observation), EINVAL);
  EXPECT_EQ(f.service.member_result(observation).error(), EINVAL);
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.service.retained(), size_t(1));
  ASSERT_EQ(f.service.submit(), 0);
  f.finish(1, 7);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_EQ(f.service.take_result(&cb)->value, ptrdiff_t(7));
  ASSERT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest,
     MemberObservationRetainsFailedPreparation) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  int cb{};
  Service::PreparedMember member;
  Service::MemberObservation observation;
  ASSERT_TRUE(bool(f.service.prepare_member(member, input(&cb))));
  f.resources.duplicate_error = EBADF;
  EXPECT_EQ(f.service
                .start_member(
                    member, [](Notification &) { return 0; }, &observation)
                .error(),
            EBADF);
  EXPECT_EQ(f.service.take_result(&cb)->error, EBADF);
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.service.retained(), size_t(1));
  EXPECT_EQ(f.service.member_result(observation)->error, EBADF);
  ASSERT_EQ(f.service.release_member(observation), 0);
  ASSERT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest, EarlyMemberReleaseDoesNotCancelIo) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  int cb{};
  Service::PreparedMember member;
  Service::MemberObservation observation;
  NotificationState notification;
  ASSERT_TRUE(bool(f.service.prepare_member(member, input(&cb))));
  ASSERT_TRUE(bool(f.service.start_member(
      member, [&](Notification &n) { return n.prepare(notification); },
      &observation)));
  ASSERT_EQ(f.service.release_member(observation), 0);
  EXPECT_EQ(*f.service.error(&cb), EINPROGRESS);
  ASSERT_EQ(f.service.submit(), 0);
  f.finish(0);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_EQ(f.service.take_result(&cb)->value, ptrdiff_t(8));
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(notification.activated, 1U);
}

TEST(LlvmLibcAioServiceRegistryTest, PreparedMemberRetainsPreparationErrors) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(4)));
  int cb[2]{};
  Service::PreparedMember members[2];
  NotificationState notifications[2];
  for (unsigned i = 0; i < 2; ++i)
    ASSERT_TRUE(bool(f.service.prepare_member(members[i], input(&cb[i]))));
  f.resources.duplicate_error = EBADF;
  EXPECT_EQ(f.service
                .start_member(members[0],
                              [&](Notification &n) {
                                return n.prepare(notifications[0]);
                              })
                .error(),
            EBADF);
  EXPECT_EQ(notifications[0].prepared, 0U);
  f.resources.duplicate_error = 0;
  notifications[1].prepare_error = ENOMEM;
  EXPECT_EQ(f.service
                .start_member(members[1],
                              [&](Notification &n) {
                                return n.prepare(notifications[1]);
                              })
                .error(),
            ENOMEM);
  EXPECT_EQ(f.ring.tail, 0U);
  EXPECT_EQ(f.service.take_result(&cb[0])->error, EBADF);
  EXPECT_EQ(f.service.take_result(&cb[1])->error, ENOMEM);
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(notifications[1].discarded, 1U);
  EXPECT_EQ(notifications[1].destroyed, 1U);
  EXPECT_EQ(f.resources.closed, 1U);
}

TEST(LlvmLibcAioServiceRegistryTest,
     PreparedMemberRejectsForeignOwnerAndReuse) {
  Fixture f, other;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  ASSERT_TRUE(bool(other.service.initialize(2)));
  int cb{};
  Service::PreparedMember member;
  ASSERT_TRUE(bool(f.service.prepare_member(member, input(&cb))));
  EXPECT_EQ(f.service.prepare_member(member, input(&cb)).error(), EINVAL);
  EXPECT_EQ(other.service.abandon_member(member), EINVAL);
  EXPECT_EQ(other.service.start_member(member, [](Notification &) { return 0; })
                .error(),
            EINVAL);
  EXPECT_EQ(f.service.retained(), size_t(1));
  ASSERT_EQ(f.service.abandon_member(member), 0);
  ASSERT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest,
     PreparedMemberPreservesConflictingBinding) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(3)));
  int cb{};
  Service::PreparedMember member;
  NotificationState first, conflicting;
  ASSERT_TRUE(bool(f.service.prepare_member(member, input(&cb))));
  ASSERT_TRUE(bool(f.admit(&cb, first)));
  EXPECT_EQ(
      f.service
          .start_member(member,
                        [&](Notification &n) { return n.prepare(conflicting); })
          .error(),
      EBUSY);
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.service.retained(), size_t(1));
  EXPECT_EQ(*f.service.error(&cb), EINPROGRESS);
  EXPECT_EQ(conflicting.discarded, 1U);
  ASSERT_EQ(f.service.submit(), 0);
  f.finish(0);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  ASSERT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest, PreparedErrorsSurviveTokenExhaustion) {
  FakeRing ring;
  Allocator allocator;
  ResourceState resources;
  uint64_t watermark = UINT64_MAX - 2;
  Service service{ring, allocator, {&resources}, watermark, 17};
  ASSERT_TRUE(bool(service.initialize(4)));
  int cb[2]{};
  Service::PreparedMember members[2];
  NotificationState notifications[2];
  for (unsigned i = 0; i < 2; ++i)
    ASSERT_TRUE(bool(service.prepare_member(members[i], input(&cb[i]))));
  EXPECT_EQ(watermark, UINT64_MAX);
  for (unsigned i = 0; i < 2; ++i) {
    EXPECT_EQ(service
                  .start_member(members[i],
                                [&](Notification &n) {
                                  return n.prepare(notifications[i]);
                                })
                  .error(),
              EOVERFLOW);
    EXPECT_EQ(service.take_result(&cb[i])->error, EOVERFLOW);
  }
  ASSERT_EQ(service.maintain(), 0);
  EXPECT_EQ(ring.tail, 0U);
  EXPECT_EQ(ring.personalities, 0U);
  EXPECT_EQ(service.retained(), size_t(0));
  EXPECT_EQ(allocator.allocated, allocator.freed);
}

TEST(LlvmLibcAioServiceRegistryTest,
     RejectedListMemberFailuresPreserveBindings) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  int cb[2]{};
  f.allocator.fail = true;
  EXPECT_EQ(f.service.admit_rejected(input(&cb[0]), EAGAIN).error(), ENOMEM);
  EXPECT_EQ(f.service.retained(), size_t(0));
  f.allocator.fail = false;
  ASSERT_TRUE(bool(f.service.admit_rejected(input(&cb[0]), EAGAIN)));
  EXPECT_EQ(f.service.admit_rejected(input(&cb[0]), EBADF).error(), EBUSY);
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(*f.service.error(&cb[0]), EAGAIN);
  EXPECT_EQ(f.service.retained(), size_t(1));
  EXPECT_EQ(f.service.admit_rejected(input(&cb[1]), EINPROGRESS).error(),
            EINVAL);
  EXPECT_EQ(f.service.retained(), size_t(1));
  ASSERT_TRUE(bool(f.service.take_result(&cb[0])));
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.ring.personalities, 0U);
  EXPECT_EQ(f.resources.closed, 0U);
}

TEST(LlvmLibcAioServiceRegistryTest,
     SyncGroupsOriginalFdDespitePrivateDuplicatesAndLaterArrivals) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(5)));
  int cb[4]{};
  NotificationState notifications[4];
  ASSERT_TRUE(bool(f.admit(&cb[0], notifications[0], Op::Write)));
  // Model fd 8 as a caller-owned alias of fd 7. It is not in fd 7's group.
  ASSERT_TRUE(bool(f.admit(&cb[1], notifications[1], Op::Read, 8)));
  ASSERT_TRUE(bool(f.admit(&cb[2], notifications[2], Op::DataSync)));
  ASSERT_TRUE(bool(f.admit(&cb[3], notifications[3], Op::Write)));
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.ring.tail, 2U);
  EXPECT_EQ(f.ring.sq[0].opcode, uint8_t(IORING_OP_WRITE));
  EXPECT_EQ(f.ring.sq[1].opcode, uint8_t(IORING_OP_READ));
  EXPECT_EQ(*f.service.error(&cb[2]), EINPROGRESS);
  f.finish(0);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.ring.tail, 3U);
  EXPECT_EQ(f.ring.sq[2].opcode, uint8_t(IORING_OP_FSYNC));
  EXPECT_EQ(f.ring.sq[2].fsync_flags, unsigned(IORING_FSYNC_DATASYNC));
  EXPECT_NE(f.ring.sq[0].fd, f.ring.sq[2].fd);
  EXPECT_EQ(*f.service.error(&cb[1]), EINPROGRESS);
  EXPECT_EQ(*f.service.error(&cb[3]), EINPROGRESS);
  ASSERT_TRUE(bool(f.service.take_result(&cb[0])));
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(notifications[0].destroyed, 1U);
  f.finish(2, 0);
  f.finish(1);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.ring.tail, 4U);
  EXPECT_EQ(f.ring.sq[3].opcode, uint8_t(IORING_OP_WRITE));
  f.finish(3);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_EQ(*f.service.error(&cb[2]), 0);
  for (unsigned i = 1; i < 4; ++i)
    ASSERT_TRUE(bool(f.service.take_result(&cb[i])));
  ASSERT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest,
     FailedSameFdPredecessorsSurviveReturnAndMaintenanceUntilDecision) {
  const bool orders[] = {false, true};
  for (bool first_has_enospc : orders) {
    Fixture f;
    ASSERT_TRUE(bool(f.service.initialize(4)));
    int cb[3]{};
    NotificationState notifications[3];
    ASSERT_TRUE(bool(f.admit(&cb[0], notifications[0], Op::Write)));
    ASSERT_TRUE(bool(f.admit(&cb[1], notifications[1], Op::Write)));
    ASSERT_TRUE(bool(f.admit(&cb[2], notifications[2], Op::Sync)));
    ASSERT_EQ(f.service.submit(), 0);
    ASSERT_EQ(f.ring.tail, 1U);
    const int first_error = first_has_enospc ? ENOSPC : EIO;
    f.finish(0, -first_error);
    ASSERT_EQ(f.service.submit(), 0);
    ASSERT_EQ(f.ring.tail, 2U);
    ASSERT_TRUE(bool(f.service.take_result(&cb[0])));
    // The pending same-fd predecessor prevents an early final decision.
    ASSERT_EQ(f.service.maintain(), 0);
    EXPECT_EQ(notifications[0].activated, 1U);
    EXPECT_EQ(notifications[0].destroyed, 0U);
    EXPECT_EQ(f.service.retained(), size_t(3));
    EXPECT_EQ(f.resources.closed, 0U);
    EXPECT_EQ(*f.service.error(&cb[2]), EINPROGRESS);
    f.finish(1, first_has_enospc ? -EIO : -ENOSPC);
    ASSERT_EQ(f.service.submit(), 0);
    EXPECT_EQ(f.ring.tail, 2U); // Failed dependency never publishes an fsync.
    EXPECT_EQ(*f.service.error(&cb[2]), first_error);
    auto sync_result = f.service.take_result(&cb[2]);
    ASSERT_TRUE(bool(sync_result));
    EXPECT_EQ(sync_result->error, first_error);
    EXPECT_EQ(sync_result->value, ssize_t(-1));
    ASSERT_TRUE(bool(f.service.take_result(&cb[1])));
    ASSERT_EQ(f.service.maintain(), 0);
    EXPECT_EQ(f.service.retained(), size_t(0));
    EXPECT_EQ(f.resources.closed, 3U);
    for (auto &notification : notifications) {
      EXPECT_EQ(notification.activated, 1U);
      EXPECT_EQ(notification.destroyed, 1U);
    }
  }
}

TEST(LlvmLibcAioServiceRegistryTest,
     UnrelatedSyncDoesNotRetainFailedAliasAfterReturn) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(4)));
  int cb[3]{};
  NotificationState notifications[3];
  ASSERT_TRUE(bool(f.admit(&cb[0], notifications[0], Op::Write)));
  ASSERT_TRUE(bool(f.admit(&cb[1], notifications[1], Op::Write, 8)));
  ASSERT_TRUE(bool(f.admit(&cb[2], notifications[2], Op::Sync)));
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.ring.tail, 2U);
  f.finish(1, -EIO);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb[1])));
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(notifications[1].destroyed, 1U);
  EXPECT_EQ(f.resources.closed, 1U);
  EXPECT_EQ(f.service.retained(), size_t(2));
  EXPECT_EQ(*f.service.error(&cb[2]), EINPROGRESS);
  EXPECT_EQ(f.ring.tail, 2U);
  f.finish(0);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.ring.tail, 3U);
  f.finish(2, 0);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_EQ(*f.service.error(&cb[2]), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb[0])));
  ASSERT_TRUE(bool(f.service.take_result(&cb[2])));
  ASSERT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest,
     SyncExcludesErrorsTerminalBeforeAdmissionAndFromLaterArrivals) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(4)));
  int cb[4]{};
  NotificationState notifications[4];
  ASSERT_TRUE(bool(f.admit(&cb[0], notifications[0], Op::Write)));
  ASSERT_EQ(f.service.submit(), 0);
  f.finish(0, -ENOSPC);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.admit(&cb[1], notifications[1], Op::Write)));
  ASSERT_TRUE(bool(f.admit(&cb[2], notifications[2], Op::Sync)));
  ASSERT_TRUE(bool(f.admit(&cb[3], notifications[3], Op::Write)));
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.ring.tail, 2U);
  EXPECT_EQ(*f.service.error(&cb[2]), EINPROGRESS);
  EXPECT_EQ(*f.service.error(&cb[3]), EINPROGRESS);
  EXPECT_EQ(*f.service.error(&cb[0]), ENOSPC);
  f.finish(1);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.ring.tail, 3U);
  EXPECT_EQ(f.ring.sq[2].opcode, uint8_t(IORING_OP_FSYNC));
  f.finish(2, 0);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.ring.tail, 4U);
  EXPECT_EQ(f.ring.sq[3].opcode, uint8_t(IORING_OP_WRITE));
  f.finish(3, -EIO);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_EQ(*f.service.error(&cb[2]), 0);
  EXPECT_EQ(*f.service.error(&cb[3]), EIO);
  ASSERT_TRUE(bool(f.service.take_result(&cb[3])));
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(notifications[3].destroyed, 1U);
  for (unsigned i = 0; i < 3; ++i)
    ASSERT_TRUE(bool(f.service.take_result(&cb[i])));
  ASSERT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest, ConstructorPreservesCompletionCallback) {
  FakeRing ring;
  Allocator allocator;
  ResourceState resources;
  uint64_t watermark = 0;
  struct CompletionState {
    unsigned calls = 0;
    const void *key = nullptr;
  } completed;
  auto callback = [](void *context, const void *key) {
    EXPECT_TRUE(CheckedGate::held);
    auto &state = *static_cast<CompletionState *>(context);
    ++state.calls;
    state.key = key;
  };
  Service service(ring, allocator, {&resources}, watermark, 17, callback,
                  &completed);
  ASSERT_TRUE(bool(service.initialize(2)));
  int cb = 0;
  NotificationState notification;
  ASSERT_TRUE(bool(service.admit(
      input(&cb), [&](Notification &n) { return n.prepare(notification); })));
  EXPECT_EQ(completed.calls, 0U);
  ASSERT_EQ(service.submit(), 0);
  ASSERT_EQ(ring.tail, 1U);
  EXPECT_EQ(completed.calls, 0U);
  ring.complete(0, 8);
  ASSERT_EQ(service.submit(), 0);
  EXPECT_EQ(completed.calls, 1U);
  EXPECT_EQ(completed.key, static_cast<const void *>(&cb));
  ASSERT_TRUE(bool(service.take_result(&cb)));
  ASSERT_EQ(service.maintain(), 0);
  EXPECT_EQ(completed.calls, 1U);
  EXPECT_EQ(service.retained(), size_t(0));
  EXPECT_EQ(resources.closed, 1U);
  EXPECT_EQ(ring.personalities, 0U);
  EXPECT_EQ(allocator.allocated, allocator.freed);
}

TEST(LlvmLibcAioServiceRegistryTest,
     CancelBatchAllocationFailureIsTransactional) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  NotificationState notification;
  int cb = 0, unknown = 0;
  ASSERT_TRUE(bool(f.admit(&cb, notification)));
  f.allocator.fail = true;
  EXPECT_EQ(f.service.prepare_cancels(7).error(), ENOMEM);
  EXPECT_FALSE(f.service.has_cancel_batches());
  EXPECT_EQ(*f.service.error(&cb), EINPROGRESS);
  f.allocator.fail = false;
  EXPECT_EQ(f.service.prepare_cancels(7, &unknown).error(), EINVAL);
  EXPECT_FALSE(f.service.has_cancel_batches());
  auto batch = f.service.prepare_cancels(7);
  ASSERT_TRUE(bool(batch));
  EXPECT_EQ(f.service.cancel_status(**batch).error(), EINVAL);
  ASSERT_EQ(f.service.attempt_cancels(**batch), 0);
  EXPECT_EQ(f.service.attempt_cancels(**batch), EINVAL);
  EXPECT_TRUE(*f.service.cancel_status(**batch) ==
              Service::CancelStatus::Canceled);
  ASSERT_EQ(f.service.finish_cancels(**batch), 0);
  EXPECT_EQ(f.service.maintain(), 0);
  EXPECT_FALSE(f.service.has_cancel_batches());
  EXPECT_EQ(*f.service.error(&cb), ECANCELED);
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  EXPECT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest,
     EmptyCancelBatchStillHasExplicitOwnership) {
  Fixture f, other;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  auto batch = f.service.prepare_cancels(7);
  ASSERT_TRUE(bool(batch));
  EXPECT_TRUE(f.service.has_cancel_batches());
  EXPECT_EQ(f.service.retained(), size_t(0));
  EXPECT_EQ(other.service.attempt_cancels(**batch), EINVAL);
  EXPECT_EQ(other.service.finish_cancels(**batch), EINVAL);
  ASSERT_EQ(f.service.attempt_cancels(**batch), 0);
  EXPECT_TRUE(*f.service.cancel_status(**batch) ==
              Service::CancelStatus::AllDone);
  EXPECT_EQ(f.service.maintain(), 0);
  EXPECT_TRUE(f.service.has_cancel_batches());
  ASSERT_EQ(f.service.finish_cancels(**batch), 0);
  EXPECT_EQ(f.service.maintain(), 0);
  EXPECT_FALSE(f.service.has_cancel_batches());
}

TEST(LlvmLibcAioServiceRegistryTest, CancelBatchSizingRaceKeepsRequestsIntact) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(3)));
  NotificationState first, second;
  int cb[2]{};
  ASSERT_TRUE(bool(f.admit(&cb[0], first)));
  struct Context {
    Fixture &fixture;
    NotificationState &notification;
    int *key;
  } context{f, second, &cb[1]};
  f.allocator.context = &context;
  f.allocator.on_allocate = [](void *p) {
    auto &c = *static_cast<Context *>(p);
    EXPECT_TRUE(bool(c.fixture.admit(c.key, c.notification)));
  };
  EXPECT_EQ(f.service.prepare_cancels(7).error(), ERANGE);
  EXPECT_FALSE(f.service.has_cancel_batches());
  EXPECT_EQ(*f.service.error(&cb[0]), EINPROGRESS);
  EXPECT_EQ(*f.service.error(&cb[1]), EINPROGRESS);
  auto retry = f.service.prepare_cancels(7);
  ASSERT_TRUE(bool(retry));
  ASSERT_EQ(f.service.attempt_cancels(**retry), 0);
  EXPECT_TRUE(*f.service.cancel_status(**retry) ==
              Service::CancelStatus::Canceled);
  ASSERT_EQ(f.service.finish_cancels(**retry), 0);
  for (int &key : cb)
    ASSERT_TRUE(bool(f.service.take_result(&key)));
  EXPECT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest,
     CancelBatchCreditFailureKeepsOtherControls) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(3)));
  NotificationState notifications[2];
  int cb[2]{};
  for (unsigned i = 0; i < 2; ++i)
    ASSERT_TRUE(bool(f.admit(&cb[i], notifications[i], Op::Read, 7 + int(i))));
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.ring.tail, 2U);
  auto batch = f.service.prepare_cancels(7);
  auto other = f.service.prepare_cancels(8);
  ASSERT_TRUE(bool(batch));
  ASSERT_TRUE(bool(other));
  ASSERT_EQ(f.service.attempt_cancels(**batch), 0);
  EXPECT_EQ(f.service.attempt_cancels(**other), EAGAIN);
  EXPECT_TRUE(*f.service.cancel_status(**batch) ==
              Service::CancelStatus::NotCanceled);
  EXPECT_TRUE(*f.service.cancel_status(**other) ==
              Service::CancelStatus::NotCanceled);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.ring.sq[2].opcode, uint8_t(IORING_OP_ASYNC_CANCEL));
  f.finish(0, -ECANCELED);
  f.finish(1, 5);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_TRUE(*f.service.cancel_status(**batch) ==
              Service::CancelStatus::Canceled);
  EXPECT_TRUE(*f.service.cancel_status(**other) ==
              Service::CancelStatus::AllDone);
  for (int &key : cb)
    ASSERT_TRUE(bool(f.service.take_result(&key)));
  ASSERT_EQ(f.service.finish_cancels(**batch), 0);
  ASSERT_EQ(f.service.finish_cancels(**other), 0);
  EXPECT_EQ(f.service.maintain(), EBUSY);
  EXPECT_TRUE(f.service.has_cancel_batches());
  f.ring.complete(2, -ENOENT);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_EQ(f.service.maintain(), 0);
  EXPECT_FALSE(f.service.has_cancel_batches());
}

TEST(LlvmLibcAioServiceRegistryTest, CancelBatchSizingMayShrinkToEmpty) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  NotificationState notification;
  int cb = 0;
  ASSERT_TRUE(bool(f.admit(&cb, notification)));
  ASSERT_EQ(f.service.submit(), 0);
  f.finish(0);
  ASSERT_EQ(f.service.submit(), 0);
  struct Context {
    Fixture &fixture;
    int *key;
  } context{f, &cb};
  f.allocator.context = &context;
  f.allocator.on_allocate = [](void *p) {
    auto &c = *static_cast<Context *>(p);
    EXPECT_TRUE(bool(c.fixture.service.take_result(c.key)));
  };
  auto batch = f.service.prepare_cancels(7);
  ASSERT_TRUE(bool(batch));
  ASSERT_EQ(f.service.attempt_cancels(**batch), 0);
  EXPECT_TRUE(*f.service.cancel_status(**batch) ==
              Service::CancelStatus::AllDone);
  EXPECT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.service.retained(), size_t(0));
  EXPECT_TRUE(f.service.has_cancel_batches());
  ASSERT_EQ(f.service.finish_cancels(**batch), 0);
  EXPECT_EQ(f.service.maintain(), 0);
  EXPECT_FALSE(f.service.has_cancel_batches());
}

TEST(LlvmLibcAioServiceRegistryTest, CancelBatchReclaimsWithdrawnControl) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  NotificationState notification;
  int cb = 0;
  ASSERT_TRUE(bool(f.admit(&cb, notification)));
  ASSERT_EQ(f.service.submit(), 0);
  auto batch = f.service.prepare_cancels(7);
  ASSERT_TRUE(bool(batch));
  ASSERT_EQ(f.service.attempt_cancels(**batch), 0);
  f.ring.submit_error = EIO;
  EXPECT_EQ(f.service.submit(), EIO);
  EXPECT_EQ(f.ring.tail, 1U);
  EXPECT_TRUE(*f.service.cancel_status(**batch) ==
              Service::CancelStatus::NotCanceled);
  ASSERT_EQ(f.service.finish_cancels(**batch), 0);
  EXPECT_EQ(f.service.maintain(), 0);
  EXPECT_FALSE(f.service.has_cancel_batches());
  EXPECT_EQ(*f.service.error(&cb), EINPROGRESS);
  EXPECT_EQ(f.resources.closed, 0U);
  f.ring.submit_error = 0;
  f.finish(0);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  EXPECT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest,
     CancelBatchReleaseMayPrecedeBothCompletions) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  NotificationState notification;
  int cb = 0;
  ASSERT_TRUE(bool(f.admit(&cb, notification)));
  ASSERT_EQ(f.service.submit(), 0);
  auto batch = f.service.prepare_cancels(7, &cb);
  ASSERT_TRUE(bool(batch));
  ASSERT_EQ(f.service.attempt_cancels(**batch), 0);
  EXPECT_TRUE(*f.service.cancel_status(**batch) ==
              Service::CancelStatus::NotCanceled);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.ring.sq[1].opcode, uint8_t(IORING_OP_ASYNC_CANCEL));
  ASSERT_EQ(f.service.finish_cancels(**batch), 0);
  EXPECT_EQ(f.service.maintain(), EBUSY);
  EXPECT_TRUE(f.service.has_cancel_batches());
  f.ring.complete(1, 0);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_EQ(f.service.maintain(), 0);
  EXPECT_FALSE(f.service.has_cancel_batches());
  EXPECT_EQ(*f.service.error(&cb), EINPROGRESS);
  EXPECT_EQ(f.resources.closed, 0U);
  f.finish(0, -ECANCELED);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  EXPECT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest, CancelBatchOriginalWinsBeforeControl) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  NotificationState notification;
  int cb = 0;
  ASSERT_TRUE(bool(f.admit(&cb, notification)));
  ASSERT_EQ(f.service.submit(), 0);
  auto batch = f.service.prepare_cancels(7);
  ASSERT_TRUE(bool(batch));
  ASSERT_EQ(f.service.attempt_cancels(**batch), 0);
  ASSERT_EQ(f.service.submit(), 0);
  f.finish(0, -ECANCELED);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_TRUE(*f.service.cancel_status(**batch) ==
              Service::CancelStatus::Canceled);
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  ASSERT_EQ(f.service.finish_cancels(**batch), 0);
  EXPECT_EQ(f.service.maintain(), EBUSY);
  EXPECT_EQ(f.service.retained(), size_t(1));
  f.ring.complete(1, 0);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_EQ(f.service.maintain(), 0);
  EXPECT_FALSE(f.service.has_cancel_batches());
  EXPECT_EQ(f.service.retained(), size_t(0));
}

TEST(LlvmLibcAioServiceRegistryTest,
     CancelBatchSuccessfulControlIsNotOriginal) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  NotificationState notification;
  int cb = 0;
  ASSERT_TRUE(bool(f.admit(&cb, notification)));
  ASSERT_EQ(f.service.submit(), 0);
  auto batch = f.service.prepare_cancels(7);
  ASSERT_TRUE(bool(batch));
  ASSERT_EQ(f.service.attempt_cancels(**batch), 0);
  ASSERT_EQ(f.service.submit(), 0);
  f.ring.complete(1, 0);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_TRUE(*f.service.cancel_status(**batch) ==
              Service::CancelStatus::NotCanceled);
  f.finish(0, 6);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_TRUE(*f.service.cancel_status(**batch) ==
              Service::CancelStatus::AllDone);
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  EXPECT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.service.retained(), size_t(1));
  ASSERT_EQ(f.service.finish_cancels(**batch), 0);
  EXPECT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest,
     CancelBatchAggregatesOnlyCapturedOriginals) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(6)));
  NotificationState old, pending, fresh;
  int cb[2]{};
  ASSERT_TRUE(bool(f.admit(&cb[0], old)));
  auto earlier = f.service.prepare_cancels(7);
  ASSERT_TRUE(bool(earlier));
  ASSERT_EQ(f.service.attempt_cancels(**earlier), 0);
  ASSERT_EQ(f.service.finish_cancels(**earlier), 0);
  ASSERT_EQ(f.service.maintain(), 0);
  // A previously canceled but unconsumed operation is already done, not a
  // new cancellation attributed to this next call.
  auto completed = f.service.prepare_cancels(7);
  ASSERT_TRUE(bool(completed));
  ASSERT_EQ(f.service.attempt_cancels(**completed), 0);
  EXPECT_TRUE(*f.service.cancel_status(**completed) ==
              Service::CancelStatus::AllDone);
  ASSERT_EQ(f.service.finish_cancels(**completed), 0);
  ASSERT_TRUE(bool(f.admit(&cb[1], pending)));
  auto mixed = f.service.prepare_cancels(7);
  ASSERT_TRUE(bool(mixed));
  ASSERT_EQ(f.service.attempt_cancels(**mixed), 0);
  EXPECT_TRUE(*f.service.cancel_status(**mixed) ==
              Service::CancelStatus::Canceled);
  ASSERT_TRUE(bool(f.service.take_result(&cb[0])));
  ASSERT_TRUE(bool(f.service.take_result(&cb[1])));
  ASSERT_TRUE(bool(f.admit(&cb[0], fresh)));
  EXPECT_TRUE(*f.service.cancel_status(**mixed) ==
              Service::CancelStatus::Canceled);
  EXPECT_EQ(*f.service.error(&cb[0]), EINPROGRESS);
  ASSERT_EQ(f.service.finish_cancels(**mixed), 0);
  EXPECT_EQ(f.service.maintain(), 0);
  auto last = f.service.prepare_cancels(7);
  ASSERT_TRUE(bool(last));
  // A prepared snapshot may be abandoned without attempting cancellation.
  ASSERT_EQ(f.service.finish_cancels(**last), 0);
  EXPECT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(*f.service.error(&cb[0]), EINPROGRESS);
  ASSERT_EQ(f.service.submit(), 0);
  f.finish(0);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb[0])));
  EXPECT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest, GrowthOomAndResultsBeyondCreditCount) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
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
    f.finish(i, int(i));
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
  ASSERT_TRUE(bool(f.service.initialize(2)));
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
  ASSERT_TRUE(bool(f.service.initialize(2)));
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
  ASSERT_TRUE(bool(f.service.initialize(2)));
  NotificationState notification;
  int cb = 0;
  Service::CancelObservation observation;
  ASSERT_TRUE(bool(f.admit(&cb, notification)));
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.service.retain_cancel(&cb, observation), 0);
  ASSERT_EQ(f.service.request_cancel(observation), 0);
  EXPECT_EQ(f.service.release_cancel(observation), EBUSY);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.ring.sq[1].opcode, uint8_t(IORING_OP_ASYNC_CANCEL));
  EXPECT_EQ(f.ring.sq[1].addr, f.ring.sq[0].user_data);
  f.ring.complete(1, 0);
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
  EXPECT_EQ(f.ring.tail, 1U);
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
  f.finish(1, 4);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  EXPECT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest, WithdrawnCancelRetainsOriginalOwnership) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  NotificationState notification;
  int cb = 0;
  Service::CancelObservation observation;
  ASSERT_TRUE(bool(f.admit(&cb, notification)));
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.service.retain_cancel(&cb, observation), 0);
  ASSERT_EQ(f.service.request_cancel(observation), 0);
  f.ring.submit_error = EIO;
  EXPECT_EQ(f.service.submit(), EIO);
  EXPECT_EQ(f.ring.tail, 1U);
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
  ASSERT_TRUE(bool(f.service.initialize(2)));
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
  ASSERT_TRUE(bool(f.service.initialize(2)));
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
  ASSERT_TRUE(bool(f.service.initialize(2)));
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

TEST(LlvmLibcAioServiceRegistryTest, ReuseKeepsOldCallbackResources) {
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
  f.ring.complete(0, -EINVAL);
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
  EXPECT_NE(f.ring.sq[0].user_data, f.ring.sq[1].user_data);
  f.finish(1, 4);
  ASSERT_EQ(f.service.submit(), 0);
  result = f.service.take_result(cb);
  ASSERT_TRUE(bool(result));
  EXPECT_EQ(result->value, ssize_t(4));
  EXPECT_EQ(f.service.maintain(), EBUSY);
  EXPECT_EQ(fresh.destroyed, 1U);
  old.callback_resources = false;
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(old.destroyed, 1U);
  EXPECT_EQ(f.resources.closed, 2U);
  EXPECT_EQ(old.activated, 1U);
}

TEST(LlvmLibcAioServiceRegistryTest, NotificationRetryDoesNotChangeResult) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
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
  ASSERT_TRUE(bool(f.service.initialize(2)));
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
  ASSERT_TRUE(bool(f.service.initialize(2)));
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
  ASSERT_TRUE(bool(f.service.initialize(2)));
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
  f.finish(1);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(second.destroyed, 1U);
}

TEST(LlvmLibcAioServiceRegistryTest,
     CompletionAdvancesDuringNotificationCleanup) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(4)));
  NotificationState first, second;
  int cb[2]{};
  ASSERT_TRUE(bool(f.admit(&cb[0], first)));
  ASSERT_TRUE(bool(f.admit(&cb[1], second)));
  ASSERT_EQ(f.service.submit(), 0);
  // Admission prepends records; use their keys to find the completed request.
  f.finish(0);
  ASSERT_EQ(f.service.submit(), 0);
  const void *done = *f.service.complete(&cb[0]) ? &cb[0] : &cb[1];
  const void *waiting = done == &cb[0] ? &cb[1] : &cb[0];
  auto &cleanup = done == &cb[0] ? first : second;
  auto &later = done == &cb[0] ? second : first;
  ASSERT_TRUE(bool(f.service.take_result(done)));
  LIBC_NAMESPACE::mmix::JumpCleanupStack::Node *head = nullptr;
  auto wait = f.service.make_wait(head);
  const void *list[] = {waiting};
  ASSERT_EQ(f.service.begin_wait(wait, list, 1), 0);
  struct Progress {
    Fixture *fixture;
    Service::Wait *wait;
    const void *key;
    unsigned calls = 0;
    static void run(void *context) {
      auto &self = *static_cast<Progress *>(context);
      auto &f = *self.fixture;
      ++self.calls;
      EXPECT_FALSE(CheckedGate::held);
      EXPECT_EQ(f.service.maintain(), EBUSY);
      EXPECT_FALSE(self.wait->completion().ready());
      // Script a separate completion worker while cleanup has not returned.
      f.finish(1, 3);
      EXPECT_EQ(f.service.submit(), 0);
      EXPECT_TRUE(self.wait->completion().ready());
      EXPECT_EQ(*f.service.error(self.key), 0);
      auto result = f.service.take_result(self.key);
      ASSERT_TRUE(bool(result));
      EXPECT_EQ(result->value, ssize_t(3));
      EXPECT_EQ(f.service.take_result(self.key).error(), EINVAL);
    }
  } progress{&f, &wait, waiting};
  cleanup.on_cleanup = Progress::run;
  cleanup.context = &progress;
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(progress.calls, 1U);
  EXPECT_EQ(wait.finish(), 0);
  EXPECT_EQ(head, nullptr);
  // The other record may have preceded the scripted completion in this pass.
  cleanup.on_cleanup = nullptr;
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(cleanup.destroyed, 1U);
  EXPECT_EQ(later.destroyed, 1U);
}

TEST(LlvmLibcAioServiceRegistryTest, OriginalCompletionLatchesServiceWait) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  NotificationState notification;
  int cb = 0;
  ASSERT_TRUE(bool(f.admit(&cb, notification)));
  LIBC_NAMESPACE::mmix::JumpCleanupStack::Node *head = nullptr;
  auto wait = f.service.make_wait(head);
  const void *list[] = {&cb, nullptr, &cb};
  ASSERT_EQ(f.service.begin_wait(wait, list, 3), 0);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_EQ(f.service.submit(), 0);
  EXPECT_FALSE(wait.completion().ready());
  f.ring.complete(0, 1);
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
  ASSERT_TRUE(bool(first.service.initialize(2)));
  ASSERT_TRUE(bool(second.service.initialize(2)));
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
  ASSERT_TRUE(bool(f.service.initialize(2)));
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
  f.finish(1);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb)));
  ASSERT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest, TypedWaitListPreservesPointerArrayType) {
  struct ControlBlock {
    unsigned sentinel;
  } cb{17}, invalid{23};
  using TypedService =
      LIBC_NAMESPACE::mmix::AioServiceRegistry<Allocator, 2, FakeRing,
                                               ResourceOps, Notification,
                                               CheckedGate, ControlBlock>;
  FakeRing ring;
  Allocator allocator;
  ResourceState resources;
  uint64_t watermark = 0;
  TypedService service{ring, allocator, {&resources}, watermark, 17};
  ASSERT_TRUE(bool(service.initialize(2)));
  NotificationState notification;
  ASSERT_TRUE(bool(service.admit(
      input(&cb), [&](Notification &n) { return n.prepare(notification); })));
  LIBC_NAMESPACE::mmix::JumpCleanupStack::Node *head = nullptr;
  auto rejected = service.make_wait(head);
  const ControlBlock *bad[] = {&cb, nullptr, &invalid};
  EXPECT_EQ(service.begin_wait(rejected, bad, 3), EINVAL);
  EXPECT_EQ(head, nullptr);
  auto wait = service.make_wait(head);
  const ControlBlock *list[] = {nullptr, &cb, &cb};
  ASSERT_EQ(service.begin_wait(wait, list, 3), 0);
  ASSERT_EQ(service.submit(), 0);
  ring.complete(0, 8);
  ASSERT_EQ(service.submit(), 0);
  ASSERT_TRUE(wait.completion().ready());
  ASSERT_TRUE(bool(service.take_result(&cb)));
  ASSERT_EQ(service.maintain(), 0);
  EXPECT_EQ(service.retained(), size_t(0));
  EXPECT_EQ(wait.finish(), 0);
  EXPECT_EQ(head, nullptr);
  EXPECT_EQ(cb.sentinel, 17U);
  EXPECT_EQ(invalid.sentinel, 23U);
  EXPECT_EQ(allocator.allocated, allocator.freed);
}

TEST(LlvmLibcAioServiceRegistryTest,
     DescriptorSnapshotPinsOnlyOriginalBindings) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(7)));
  int cb[3]{};
  NotificationState notifications[4];
  for (unsigned i = 0; i < 3; ++i) {
    auto request = input(&cb[i]);
    request.descriptor = i == 1 ? 8 : 7;
    ASSERT_TRUE(bool(f.service.admit(request, [&](Notification &n) {
      return n.prepare(notifications[i]);
    })));
  }
  EXPECT_EQ(*f.service.cancel_count(7), size_t(2));
  EXPECT_EQ(*f.service.cancel_count(8), size_t(1));
  EXPECT_EQ(*f.service.cancel_count(9), size_t(0));
  Service::CancelObservation snapshot[2];
  EXPECT_EQ(f.service.retain_cancels(7, snapshot, 1).error(), ERANGE);
  EXPECT_EQ(f.service.retain_cancels(7, nullptr, 2).error(), EINVAL);
  EXPECT_EQ(*f.service.retain_cancels(9, nullptr, 0), size_t(0));
  auto selected = f.service.retain_cancels(7, snapshot, 2);
  ASSERT_TRUE(bool(selected));
  ASSERT_EQ(*selected, size_t(2));
  EXPECT_EQ(f.service.retain_cancels(7, snapshot, 2).error(), EINVAL);
  for (auto &observation : snapshot) {
    ASSERT_EQ(f.service.request_cancel(observation), 0);
    auto original = f.service.original_cancel_result(observation);
    ASSERT_TRUE(bool(original));
    EXPECT_EQ(original->error, ECANCELED);
  }
  EXPECT_EQ(*f.service.error(&cb[1]), EINPROGRESS);
  ASSERT_TRUE(bool(f.service.take_result(&cb[0])));
  ASSERT_TRUE(bool(f.service.take_result(&cb[2])));
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.service.retained(), size_t(3));
  ASSERT_TRUE(bool(f.admit(&cb[0], notifications[3])));
  EXPECT_EQ(*f.service.cancel_count(7), size_t(1));
  for (auto &observation : snapshot) {
    EXPECT_EQ(f.service.original_cancel_result(observation)->error, ECANCELED);
    ASSERT_EQ(f.service.release_cancel(observation), 0);
  }
  EXPECT_EQ(*f.service.error(&cb[0]), EINPROGRESS);
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.service.retained(), size_t(2));
  ASSERT_EQ(f.service.submit(), 0);
  f.finish(0);
  f.finish(1);
  ASSERT_EQ(f.service.submit(), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb[0])));
  ASSERT_TRUE(bool(f.service.take_result(&cb[1])));
  ASSERT_EQ(f.service.maintain(), 0);
}

TEST(LlvmLibcAioServiceRegistryTest,
     SnapshotIncludesOnlyUnconsumedCompletions) {
  Fixture f;
  ASSERT_TRUE(bool(f.service.initialize(2)));
  int cb[2]{};
  NotificationState notifications[2];
  for (unsigned i = 0; i < 2; ++i) {
    ASSERT_TRUE(bool(f.admit(&cb[i], notifications[i])));
    ASSERT_EQ(f.service.submit(), 0);
    f.finish(i, int(i + 1));
    ASSERT_EQ(f.service.submit(), 0);
  }
  ASSERT_TRUE(bool(f.service.take_result(&cb[0])));
  Service::CancelObservation observation;
  ASSERT_EQ(*f.service.cancel_count(7), size_t(1));
  ASSERT_EQ(*f.service.retain_cancels(7, &observation, 1), size_t(1));
  ASSERT_EQ(f.service.request_cancel(observation), 0);
  ASSERT_TRUE(bool(f.service.take_result(&cb[1])));
  ASSERT_EQ(*f.service.cancel_count(7), size_t(0));
  ASSERT_EQ(f.service.maintain(), 0);
  EXPECT_EQ(f.service.retained(), size_t(1));
  auto original = f.service.original_cancel_result(observation);
  ASSERT_TRUE(bool(original));
  EXPECT_EQ(original->error, 0);
  EXPECT_EQ(original->value, ptrdiff_t(2));
  ASSERT_EQ(f.service.release_cancel(observation), 0);
  ASSERT_EQ(f.service.maintain(), 0);
}
