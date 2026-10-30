//===-- Tests for private bounded AIO wait observers --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/aio_wait_set.h"
#include "test/UnitTest/Test.h"

using Request = LIBC_NAMESPACE::AioRequest;
using WaitSet = LIBC_NAMESPACE::AioWaitSet;
using LIBC_NAMESPACE::ErrorOr;

namespace {
struct Binding {
  const void *control_block;
  Request *request;
};

struct Resolver {
  Binding *bindings;
  size_t count;
  size_t calls = 0;

  Request *operator()(const void *control_block) {
    ++calls;
    for (size_t i = 0; i < count; ++i)
      if (bindings[i].control_block == control_block)
        return bindings[i].request;
    return nullptr;
  }
};

template <typename T> void expect_error(ErrorOr<T> result, int error) {
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error);
}

void expect_ready(ErrorOr<bool> result, bool ready) {
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, ready);
}

void expect_result(ErrorOr<Request::Result> result, ssize_t value, int error) {
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->value, value);
  EXPECT_EQ(result->error, error);
}
} // namespace

TEST(LlvmLibcAioWaitSetTest, PendingToAnyAndAllInEitherCompletionOrder) {
  const bool orders[] = {false, true};
  for (bool reverse : orders) {
    int cb[2]{};
    Request first({&cb[0], 17, 1}, false, 1);
    Request second({&cb[1], 17, 2}, false, 1);
    Binding bindings[] = {{&cb[0], &first}, {&cb[1], &second}};
    Resolver resolve{bindings, 2};
    const int *list[] = {&cb[0], &cb[1]};
    WaitSet::Slot slots[2];
    WaitSet set(slots, 2);
    ASSERT_TRUE(first.accept().has_value());
    ASSERT_TRUE(second.accept().has_value());
    ASSERT_TRUE(first.mark_published(1));
    ASSERT_TRUE(second.mark_published(2));
    EXPECT_FALSE(set.attached());
    expect_error(set.any_complete(), EINVAL);
    expect_error(set.all_complete(), EINVAL);
    expect_error(set.result(0), EINVAL);
    ASSERT_TRUE(set.attach(list, 2, resolve).has_value());
    EXPECT_TRUE(set.attached());
    EXPECT_EQ(set.size(), size_t(2));
    expect_ready(set.any_complete(), false);
    expect_ready(set.all_complete(), false);
    expect_error(set.result(0), EINPROGRESS);
    expect_error(set.result(1), EINPROGRESS);
    expect_error(set.result(2), EINVAL);
    Request &early = reverse ? second : first;
    Request &late = reverse ? first : second;
    ASSERT_TRUE(early.complete(early.identity().token, {7, 0}));
    expect_ready(set.any_complete(), true);
    expect_ready(set.all_complete(), false);
    expect_result(set.result(reverse ? 1 : 0), 7, 0);
    expect_result(early.aio_return(), 7, 0);
    EXPECT_FALSE(early.reclaimable());
    expect_ready(set.any_complete(), true);
    expect_ready(set.all_complete(), false);
    ASSERT_TRUE(late.complete(late.identity().token, {9, 0}));
    expect_ready(set.any_complete(), true);
    expect_ready(set.all_complete(), true);
    expect_result(set.result(reverse ? 0 : 1), 9, 0);
    EXPECT_EQ(resolve.calls, size_t(2));
    set.detach();
    EXPECT_TRUE(early.reclaimable());
    EXPECT_FALSE(late.reclaimable());
    expect_result(late.aio_return(), 9, 0);
    EXPECT_TRUE(late.reclaimable());
  }
}

TEST(LlvmLibcAioWaitSetTest, EmptyAndAllNullAreNotAnyCompletion) {
  Resolver resolve{nullptr, 0};
  WaitSet set(nullptr, 0);
  const int *const *empty = nullptr;
  const int *nulls[] = {nullptr, nullptr, nullptr};
  ASSERT_TRUE(set.attach(empty, 0, resolve).has_value());
  EXPECT_TRUE(set.attached());
  EXPECT_EQ(set.size(), size_t(0));
  expect_ready(set.any_complete(), false);
  expect_ready(set.all_complete(), true);
  expect_error(set.result(0), EINVAL);
  expect_error(set.attach(nulls, 3, resolve), EBUSY);
  set.detach();
  ASSERT_TRUE(set.attach(nulls, 3, resolve).has_value());
  expect_ready(set.any_complete(), false);
  expect_ready(set.all_complete(), true);
  EXPECT_EQ(set.size(), size_t(0));
  EXPECT_EQ(resolve.calls, size_t(0));
  set.detach();
  set.detach();
  EXPECT_FALSE(set.attached());
  expect_error(set.any_complete(), EINVAL);
  expect_error(set.all_complete(), EINVAL);
}

TEST(LlvmLibcAioWaitSetTest, DuplicatePointersAndNullsDoNotConsumeCapacity) {
  int cb[3]{};
  Request first({&cb[0], 17, 1}, false, 1);
  Request second({&cb[1], 17, 2}, false, 1);
  // Two trusted lookup keys intentionally resolve to the same request.
  Binding bindings[] = {{&cb[0], &first}, {&cb[1], &second}, {&cb[2], &first}};
  Resolver resolve{bindings, 3};
  const int *list[] = {nullptr, &cb[1],  &cb[0], &cb[2],
                       &cb[1],  nullptr, &cb[0], &cb[2]};
  WaitSet::Slot slots[2];
  WaitSet set(slots, 2);
  ASSERT_TRUE(first.accept().has_value());
  ASSERT_TRUE(second.reject_with_result(EAGAIN).has_value());
  ASSERT_TRUE(set.attach(list, 8, resolve).has_value());
  EXPECT_EQ(set.size(), size_t(2));
  EXPECT_EQ(resolve.calls, size_t(6));
  expect_result(set.result(0), -1, EAGAIN);
  expect_error(set.result(1), EINPROGRESS);
  expect_ready(set.any_complete(), true);
  expect_ready(set.all_complete(), false);
  Request::Reference extra;
  expect_error(first.retain(extra), EAGAIN);
  expect_error(second.retain(extra), EAGAIN);
  set.detach();
  ASSERT_TRUE(first.retain(extra).has_value());
  ASSERT_TRUE(first.release(extra));
  ASSERT_TRUE(first.complete_unpublished(ECANCELED));
  expect_result(first.aio_return(), -1, ECANCELED);
  expect_result(second.aio_return(), -1, EAGAIN);
  EXPECT_TRUE(first.reclaimable());
  EXPECT_TRUE(second.reclaimable());
}

TEST(LlvmLibcAioWaitSetTest, InvalidBindingRollsBackEarlierCompletedObserver) {
  int cb[2]{};
  Request request({&cb[0], 17, 1}, false, 2);
  Binding binding{&cb[0], &request};
  Resolver resolve{&binding, 1};
  const int *list[] = {&cb[0], nullptr, &cb[0], &cb[1]};
  WaitSet::Slot slots[2];
  WaitSet set(slots, 2);
  Request::Reference existing;
  ASSERT_TRUE(request.reject_with_result(EIO).has_value());
  ASSERT_TRUE(request.retain(existing).has_value());
  expect_result(request.aio_return(), -1, EIO);
  expect_error(set.attach(list, 4, resolve), EINVAL);
  EXPECT_FALSE(set.attached());
  EXPECT_EQ(set.size(), size_t(0));
  expect_error(set.result(0), EINVAL);
  expect_result(request.result(existing), -1, EIO);
  ASSERT_TRUE(request.release(existing));
  EXPECT_TRUE(request.reclaimable());
  set.detach();
}

TEST(LlvmLibcAioWaitSetTest, InvalidRequestRollsBackAndStorageCanBeReused) {
  int cb[2]{};
  Request first({&cb[0], 17, 1}, false, 1);
  Request unaccepted({&cb[1], 17, 2}, false, 1);
  Binding bindings[] = {{&cb[0], &first}, {&cb[1], &unaccepted}};
  Resolver resolve{bindings, 2};
  const int *list[] = {&cb[0], &cb[1]};
  WaitSet::Slot slots[2];
  WaitSet set(slots, 2);
  ASSERT_TRUE(first.accept().has_value());
  expect_error(set.attach(list, 2, resolve), EINVAL);
  EXPECT_FALSE(set.attached());
  EXPECT_EQ(set.size(), size_t(0));
  ASSERT_TRUE(unaccepted.accept().has_value());
  ASSERT_TRUE(set.attach(list, 2, resolve).has_value());
  set.detach();
  ASSERT_TRUE(first.complete_unpublished(ECANCELED));
  ASSERT_TRUE(unaccepted.complete_unpublished(ECANCELED));
  expect_result(first.aio_return(), -1, ECANCELED);
  expect_result(unaccepted.aio_return(), -1, ECANCELED);
  EXPECT_TRUE(first.reclaimable());
  EXPECT_TRUE(unaccepted.reclaimable());
}

TEST(LlvmLibcAioWaitSetTest, HolderExhaustionRollsBackWithoutReleasingOthers) {
  int cb[2]{};
  Request first({&cb[0], 17, 1}, false, 1);
  Request second({&cb[1], 17, 2}, false, 1);
  Binding bindings[] = {{&cb[0], &first}, {&cb[1], &second}};
  Resolver resolve{bindings, 2};
  const int *list[] = {&cb[0], &cb[1]};
  WaitSet::Slot slots[2];
  WaitSet set(slots, 2);
  Request::Reference other;
  ASSERT_TRUE(first.accept().has_value());
  ASSERT_TRUE(second.accept().has_value());
  ASSERT_TRUE(second.retain(other).has_value());
  expect_error(set.attach(list, 2, resolve), EAGAIN);
  EXPECT_FALSE(set.attached());
  EXPECT_EQ(set.size(), size_t(0));
  expect_error(second.result(other), EINPROGRESS);
  ASSERT_TRUE(second.release(other));
  ASSERT_TRUE(set.attach(list, 2, resolve).has_value());
  set.detach();
  ASSERT_TRUE(first.complete_unpublished(ECANCELED));
  ASSERT_TRUE(second.complete_unpublished(ECANCELED));
  expect_result(first.aio_return(), -1, ECANCELED);
  expect_result(second.aio_return(), -1, ECANCELED);
  EXPECT_TRUE(first.reclaimable());
  EXPECT_TRUE(second.reclaimable());
}

TEST(LlvmLibcAioWaitSetTest, UniqueCapacityExhaustionRollsBack) {
  int cb[2]{};
  Request first({&cb[0], 17, 1}, false, 1);
  Request second({&cb[1], 17, 2}, false, 1);
  Binding bindings[] = {{&cb[0], &first}, {&cb[1], &second}};
  Resolver resolve{bindings, 2};
  const int *list[] = {&cb[0], &cb[0], nullptr, &cb[1]};
  WaitSet::Slot slot;
  WaitSet set(&slot, 1);
  WaitSet zero(nullptr, 0);
  ASSERT_TRUE(first.accept().has_value());
  ASSERT_TRUE(second.accept().has_value());
  expect_error(zero.attach(list, 4, resolve), EAGAIN);
  EXPECT_FALSE(zero.attached());
  expect_error(set.attach(list, 4, resolve), EAGAIN);
  EXPECT_FALSE(set.attached());
  EXPECT_EQ(set.size(), size_t(0));
  ASSERT_TRUE(set.attach(list, 3, resolve).has_value());
  EXPECT_EQ(set.size(), size_t(1));
  set.detach();
  ASSERT_TRUE(first.complete_unpublished(ECANCELED));
  ASSERT_TRUE(second.complete_unpublished(ECANCELED));
  expect_result(first.aio_return(), -1, ECANCELED);
  expect_result(second.aio_return(), -1, ECANCELED);
  EXPECT_TRUE(first.reclaimable());
  EXPECT_TRUE(second.reclaimable());
}

TEST(LlvmLibcAioWaitSetTest, InvalidListAndStorageDoNotInvokeResolver) {
  const int *const *missing = nullptr;
  Resolver resolve{nullptr, 0};
  WaitSet::Slot slot;
  WaitSet set(&slot, 1);
  WaitSet invalid(nullptr, 1);
  expect_error(set.attach(missing, 1, resolve), EINVAL);
  expect_error(invalid.attach(missing, 0, resolve), EINVAL);
  EXPECT_FALSE(set.attached());
  EXPECT_FALSE(invalid.attached());
  EXPECT_EQ(resolve.calls, size_t(0));
  ASSERT_TRUE(set.attach(missing, 0, resolve).has_value());
  set.detach();
}

TEST(LlvmLibcAioWaitSetTest, ConsumedResultSurvivesControlBlockAddressReuse) {
  int cb;
  Request old_request({&cb, 17, 1}, false, 1);
  Request new_request({&cb, 17, 2}, false, 1);
  Binding binding{&cb, &old_request};
  Resolver resolve{&binding, 1};
  const int *list[] = {&cb};
  WaitSet::Slot old_slot, new_slot;
  WaitSet old_set(&old_slot, 1), new_set(&new_slot, 1);
  ASSERT_TRUE(old_request.accept().has_value());
  ASSERT_TRUE(old_request.mark_published(1));
  ASSERT_TRUE(old_set.attach(list, 1, resolve).has_value());
  ASSERT_TRUE(old_request.complete(1, {5, 0}));
  expect_result(old_request.aio_return(), 5, 0);
  EXPECT_EQ(old_request.aio_error(), EINVAL);
  EXPECT_FALSE(old_request.reclaimable());
  // Model AioEngine::take_result unbinding followed by fresh admission.
  binding.request = &new_request;
  ASSERT_TRUE(new_request.accept().has_value());
  ASSERT_TRUE(new_request.mark_published(2));
  ASSERT_TRUE(new_set.attach(list, 1, resolve).has_value());
  expect_ready(old_set.any_complete(), true);
  expect_ready(old_set.all_complete(), true);
  expect_result(old_set.result(0), 5, 0);
  expect_ready(new_set.any_complete(), false);
  expect_ready(new_set.all_complete(), false);
  expect_error(new_set.result(0), EINPROGRESS);
  ASSERT_TRUE(new_request.complete(2, {-1, EIO}));
  expect_result(new_request.aio_return(), -1, EIO);
  expect_result(new_set.result(0), -1, EIO);
  expect_result(old_set.result(0), 5, 0);
  EXPECT_EQ(resolve.calls, size_t(2));
  old_set.detach();
  EXPECT_TRUE(old_request.reclaimable());
  EXPECT_FALSE(new_request.reclaimable());
  new_set.detach();
  EXPECT_TRUE(new_request.reclaimable());
}

TEST(LlvmLibcAioWaitSetTest,
     ConsumedResultCanBeObservedWhileAnotherOwnerPinsIt) {
  int cb;
  Request request({&cb, 17, 1}, false, 2);
  Binding binding{&cb, &request};
  Resolver resolve{&binding, 1};
  const int *list[] = {&cb};
  WaitSet::Slot slot;
  WaitSet set(&slot, 1);
  Request::Reference existing;
  ASSERT_TRUE(request.reject_with_result(EBADF).has_value());
  ASSERT_TRUE(request.retain(existing).has_value());
  expect_result(request.aio_return(), -1, EBADF);
  // An internal list group can resolve a pinned request after public unbinding.
  ASSERT_TRUE(set.attach(list, 1, resolve).has_value());
  ASSERT_TRUE(request.release(existing));
  expect_ready(set.any_complete(), true);
  expect_ready(set.all_complete(), true);
  expect_result(set.result(0), -1, EBADF);
  expect_error(request.aio_return(), EINVAL);
  EXPECT_FALSE(request.reclaimable());
  set.detach();
  EXPECT_TRUE(request.reclaimable());
  expect_error(set.attach(list, 1, resolve), EINVAL);
  EXPECT_FALSE(set.attached());
  EXPECT_EQ(set.size(), size_t(0));
}

TEST(LlvmLibcAioWaitSetTest, ErrorCompletionsAreReadyWithoutConsumingResults) {
  const int errors[] = {EIO, ECANCELED, EAGAIN};
  for (int error : errors) {
    int cb;
    Request request({&cb, 17, 1}, false, 1);
    Binding binding{&cb, &request};
    Resolver resolve{&binding, 1};
    const int *list[] = {&cb};
    WaitSet::Slot slot;
    WaitSet set(&slot, 1);
    ASSERT_TRUE(request.accept().has_value());
    ASSERT_TRUE(set.attach(list, 1, resolve).has_value());
    ASSERT_TRUE(request.complete_unpublished(error));
    expect_ready(set.any_complete(), true);
    expect_ready(set.all_complete(), true);
    expect_result(set.result(0), -1, error);
    expect_result(set.result(0), -1, error);
    EXPECT_EQ(request.aio_error(), error);
    set.detach();
    EXPECT_FALSE(request.reclaimable());
    expect_result(request.aio_return(), -1, error);
    EXPECT_TRUE(request.reclaimable());
  }
}

TEST(LlvmLibcAioWaitSetTest, DetachPreservesIoResultAndNotificationOwnership) {
  int cb;
  Request request({&cb, 17, 1}, true, 1);
  Binding binding{&cb, &request};
  Resolver resolve{&binding, 1};
  const int *list[] = {&cb};
  WaitSet::Slot slot;
  WaitSet set(&slot, 1);
  ASSERT_TRUE(request.accept().has_value());
  ASSERT_TRUE(request.mark_published(1));
  ASSERT_TRUE(set.attach(list, 1, resolve).has_value());
  expect_error(set.attach(list, 1, resolve), EBUSY);
  EXPECT_EQ(resolve.calls, size_t(1));
  EXPECT_EQ(set.size(), size_t(1));
  expect_error(set.result(0), EINPROGRESS);
  set.detach();
  set.detach();
  EXPECT_FALSE(set.attached());
  EXPECT_EQ(set.size(), size_t(0));
  expect_error(set.result(0), EINVAL);
  EXPECT_EQ(request.aio_error(), EINPROGRESS);
  expect_error(request.aio_return(), EINPROGRESS);
  expect_error(request.take_notification(), EINPROGRESS);
  EXPECT_FALSE(request.reclaimable());
  // Reattach the same set and storage, then detach after completion as well.
  ASSERT_TRUE(set.attach(list, 1, resolve).has_value());
  ASSERT_TRUE(request.complete(1, {3, 0}));
  expect_result(set.result(0), 3, 0);
  set.detach();
  EXPECT_EQ(request.aio_error(), 0);
  expect_result(request.aio_return(), 3, 0);
  EXPECT_FALSE(request.reclaimable());
  auto notification = request.take_notification();
  ASSERT_TRUE(notification.has_value());
  EXPECT_TRUE(*notification == Request::NotificationDecision::Activate);
  ASSERT_TRUE(request.release_notification());
  EXPECT_TRUE(request.reclaimable());
  // The detached slots can also be supplied to another set.
  WaitSet replacement(&slot, 1);
  ASSERT_TRUE(replacement.attach(list, 0, resolve).has_value());
  expect_ready(replacement.any_complete(), false);
  expect_ready(replacement.all_complete(), true);
  replacement.detach();
}
