//===-- Tests for private AIO request ownership -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/aio_request.h"
#include "test/UnitTest/Test.h"

using Request = LIBC_NAMESPACE::AioRequest;
using Reference = Request::Reference;
using Cancel = Request::CancelControl;
using Decision = Request::NotificationDecision;
using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;

namespace {
constexpr uint64_t ORIGINAL = UINT64_C(0x8123456789abcdef);
constexpr uint64_t CANCEL = ORIGINAL + 1;
constexpr bool BOTH_ORDERS[] = {false, true};

struct Fixture {
  int control_block = 0;
  Request request;
  explicit Fixture(bool notify = false, uint32_t holders = 4,
                   uint64_t token = ORIGINAL)
      : request({&control_block, 17, token}, notify, holders) {}
};

template <typename T> void expect_error(ErrorOr<T> result, int error) {
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error);
}

void expect_result(ErrorOr<Request::Result> result, ssize_t value, int error) {
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->value, value);
  EXPECT_EQ(result->error, error);
}

// A preparation failure is injected before acceptance, not as a synthetic CQE.
// The actual allocation/thread startup transaction belongs to the service.
struct OneShot {
  bool suspended = false;
  unsigned activations = 0;
  unsigned disposals = 0;

  ErrorOr<void> prepare(int error) {
    if (error != 0)
      return Error(error);
    suspended = true;
    return {};
  }

  void finish(Decision decision) {
    EXPECT_TRUE(suspended);
    suspended = false;
    if (decision == Decision::Activate)
      ++activations;
    else
      ++disposals;
  }
};

ErrorOr<void> admit(Request &request, OneShot &callback,
                    int preparation_error) {
  auto prepared = callback.prepare(preparation_error);
  if (!prepared.has_value()) {
    EXPECT_TRUE(request.reject());
    return Error(prepared.error());
  }
  return request.accept();
}
} // namespace

TEST(LlvmLibcAioRequestTest, IdentityAndSingleResultRetrieval) {
  int control_block;
  Request::Identity identity = {&control_block, 17, ORIGINAL};
  Request request(identity, false, 0);
  identity = {nullptr, 0, 0};
  EXPECT_EQ(request.identity().control_block,
            static_cast<const void *>(&control_block));
  EXPECT_EQ(request.identity().process, uint64_t(17));
  EXPECT_EQ(request.identity().token, ORIGINAL);
  EXPECT_FALSE(request.reclaimable());
  EXPECT_EQ(request.aio_error(), EINVAL);
  expect_error(request.aio_return(), EINVAL);
  ASSERT_TRUE(request.accept().has_value());
  expect_error(request.accept(), EBUSY);
  EXPECT_FALSE(request.reject());
  EXPECT_EQ(request.aio_error(), EINPROGRESS);
  expect_error(request.aio_return(), EINPROGRESS);
  EXPECT_FALSE(request.reclaimable());
  ASSERT_TRUE(request.mark_published(ORIGINAL));
  ASSERT_TRUE(request.complete(ORIGINAL, {7, 0}));
  EXPECT_EQ(request.aio_error(), 0);
  EXPECT_EQ(request.aio_error(), 0);
  EXPECT_FALSE(request.reclaimable());
  expect_result(request.aio_return(), 7, 0);
  expect_error(request.aio_return(), EINVAL);
  EXPECT_EQ(request.aio_error(), EINVAL);
  EXPECT_TRUE(request.reclaimable());
  EXPECT_FALSE(request.complete(ORIGINAL, {-1, EIO}));
  expect_error(request.accept(), EBUSY);
  Reference late;
  expect_error(request.retain(late), EINVAL);
}

TEST(LlvmLibcAioRequestTest, ListAdmissionFailureRemainsQueryable) {
  const int errors[] = {EAGAIN, EINVAL, EBADF};
  for (bool notify : BOTH_ORDERS) {
    for (int error : errors) {
      Fixture fixture(notify);
      auto &request = fixture.request;
      ASSERT_TRUE(request.reject_with_result(error).has_value());
      EXPECT_EQ(request.aio_error(), error);
      EXPECT_FALSE(request.reclaimable());
      EXPECT_FALSE(request.reject());
      expect_error(request.accept(), EBUSY);
      expect_error(request.reject_with_result(EIO), EBUSY);
      EXPECT_FALSE(request.mark_published(ORIGINAL));
      EXPECT_FALSE(request.complete_unpublished(ECANCELED));
      EXPECT_FALSE(request.complete_withdrawn(ORIGINAL, EIO));
      EXPECT_FALSE(request.complete(ORIGINAL, {0, 0}));
      expect_error(request.take_notification(), EINVAL);
      expect_error(request.take_notification(true), EINVAL);
      Reference group;
      ASSERT_TRUE(request.retain(group).has_value());
      expect_result(request.aio_return(), -1, error);
      expect_error(request.aio_return(), EINVAL);
      EXPECT_FALSE(request.reclaimable());
      expect_result(request.result(group), -1, error);
      ASSERT_TRUE(request.release(group));
      EXPECT_TRUE(request.reclaimable());
    }
  }
}

TEST(LlvmLibcAioRequestTest, ListAdmissionFailureCannotReplaceAcceptedWork) {
  Fixture fixture;
  auto &request = fixture.request;
  expect_error(request.reject_with_result(0), EINVAL);
  expect_error(request.reject_with_result(-EIO), EINVAL);
  expect_error(request.reject_with_result(EINPROGRESS), EINVAL);
  ASSERT_TRUE(request.accept().has_value());
  expect_error(request.reject_with_result(EAGAIN), EBUSY);
  EXPECT_EQ(request.aio_error(), EINPROGRESS);
  ASSERT_TRUE(request.mark_published(ORIGINAL));
  ASSERT_TRUE(request.complete(ORIGINAL, {3, 0}));
  expect_result(request.aio_return(), 3, 0);
}

TEST(LlvmLibcAioRequestTest, ListAdmissionFailureRequiresTrustedIdentity) {
  int address;
  const Request::Identity identities[] = {
      {nullptr, 1, 1}, {&address, 0, 1}, {&address, 1, 0}};
  for (auto identity : identities) {
    Request request(identity, false, 0);
    expect_error(request.reject_with_result(EAGAIN), EINVAL);
    EXPECT_TRUE(request.reject());
    EXPECT_TRUE(request.reclaimable());
  }
}

TEST(LlvmLibcAioRequestTest, FailedAdmissionHasNoCompletionOrNotification) {
  const int errors[] = {ENOMEM, EAGAIN, EINVAL};
  for (int error : errors) {
    Fixture fixture(true);
    Request &request = fixture.request;
    OneShot callback;
    expect_error(admit(request, callback, error), error);
    EXPECT_TRUE(request.reclaimable());
    EXPECT_FALSE(callback.suspended);
    EXPECT_EQ(callback.activations, 0U);
    EXPECT_EQ(callback.disposals, 0U);
    EXPECT_FALSE(request.reject());
    expect_error(request.accept(), EBUSY);
    EXPECT_EQ(request.aio_error(), EINVAL);
    expect_error(request.aio_return(), EINVAL);
    EXPECT_FALSE(request.mark_published(ORIGINAL));
    EXPECT_FALSE(request.complete_unpublished(ECANCELED));
    EXPECT_FALSE(request.complete_withdrawn(ORIGINAL, EIO));
    EXPECT_FALSE(request.complete(ORIGINAL, {0, 0}));
    expect_error(request.take_notification(), EINVAL);
    expect_error(request.take_notification(true), EINVAL);
    EXPECT_FALSE(request.release_notification());
    Reference reference;
    Cancel cancel(CANCEL);
    expect_error(request.retain(reference), EINVAL);
    expect_error(request.prepare_cancel(cancel), EINVAL);
    EXPECT_TRUE(cancel.reclaimable());
  }
  // A later admission stage may fail after successful callback precreation.
  Fixture fixture(true);
  OneShot callback;
  ASSERT_TRUE(callback.prepare(0).has_value());
  ASSERT_TRUE(fixture.request.reject());
  callback.finish(Decision::Suppress);
  EXPECT_TRUE(fixture.request.reclaimable());
  EXPECT_EQ(callback.activations, 0U);
  EXPECT_EQ(callback.disposals, 1U);
}

TEST(LlvmLibcAioRequestTest, InvalidIdentityCannotBeAccepted) {
  int control_block;
  const Request::Identity invalid[] = {{nullptr, 17, ORIGINAL},
                                       {&control_block, 0, ORIGINAL},
                                       {&control_block, 17, 0}};
  for (auto identity : invalid) {
    Request request(identity, false, 0);
    expect_error(request.accept(), EINVAL);
    EXPECT_EQ(request.aio_error(), EINVAL);
    ASSERT_TRUE(request.reject());
    EXPECT_TRUE(request.reclaimable());
  }
}

TEST(LlvmLibcAioRequestTest, MalformedAndDuplicateCompletionsDoNotPublish) {
  Fixture fixture;
  Request &request = fixture.request;
  EXPECT_FALSE(request.complete(ORIGINAL, {0, 0}));
  ASSERT_TRUE(request.accept().has_value());
  ASSERT_TRUE(request.mark_published(ORIGINAL));
  EXPECT_FALSE(request.complete(0, {0, 0}));
  EXPECT_FALSE(request.complete(CANCEL, {0, 0}));
  const Request::Result invalid[] = {
      {-1, 0}, {1, EIO}, {-2, EIO}, {-1, -EIO}, {-1, EINPROGRESS}};
  for (auto result : invalid) {
    EXPECT_FALSE(request.complete(ORIGINAL, result));
    EXPECT_EQ(request.aio_error(), EINPROGRESS);
    expect_error(request.aio_return(), EINPROGRESS);
    EXPECT_FALSE(request.reclaimable());
  }
  ASSERT_TRUE(request.complete(ORIGINAL, {-1, EIO}));
  EXPECT_FALSE(request.complete(ORIGINAL, {123, 0}));
  EXPECT_EQ(request.aio_error(), EIO);
  expect_result(request.aio_return(), -1, EIO);
  expect_error(request.aio_return(), EINVAL);
  EXPECT_TRUE(request.reclaimable());
}

TEST(LlvmLibcAioRequestTest, QueuedCompletionRetainsDependencyAndNotification) {
  const int errors[] = {ECANCELED, EIO, EAGAIN};
  for (int error : errors) {
    for (bool notification_first : BOTH_ORDERS) {
      Fixture fixture(true, 2);
      Request &request = fixture.request;
      OneShot callback;
      Reference dependency_wait, observer;
      Cancel cancel(CANCEL), late(CANCEL + 1);
      ASSERT_TRUE(admit(request, callback, 0).has_value());
      ASSERT_TRUE(request.retain(dependency_wait).has_value());
      ASSERT_TRUE(request.prepare_cancel(cancel).has_value());
      expect_error(request.retain(observer), EAGAIN);
      EXPECT_FALSE(request.publish_cancel(cancel));
      EXPECT_FALSE(request.complete_cancel(cancel, CANCEL, 0));
      expect_error(request.result(dependency_wait), EINPROGRESS);
      expect_error(request.take_notification(), EINPROGRESS);

      // No original is submitted while waiting for the dependency.
      ASSERT_TRUE(request.complete_unpublished(error));
      EXPECT_EQ(request.aio_error(), error);
      EXPECT_FALSE(request.mark_published(ORIGINAL));
      EXPECT_FALSE(request.complete_unpublished(EINVAL));
      EXPECT_FALSE(request.complete_withdrawn(ORIGINAL, EINVAL));
      EXPECT_FALSE(request.complete(ORIGINAL, {8, 0}));
      EXPECT_FALSE(request.publish_cancel(cancel));
      EXPECT_FALSE(request.complete_cancel(cancel, CANCEL, 0));
      EXPECT_FALSE(request.release_cancel(cancel));
      expect_error(request.prepare_cancel(late), EINVAL);
      expect_result(request.original_result(cancel), -1, error);
      expect_error(request.cancel_result(cancel), EINPROGRESS);
      EXPECT_FALSE(request.reclaimable());
      ASSERT_TRUE(request.abort_cancel(cancel));
      EXPECT_TRUE(cancel.reclaimable());
      expect_result(request.aio_return(), -1, error);
      expect_error(request.aio_return(), EINVAL);
      EXPECT_FALSE(request.reclaimable());
      expect_result(request.result(dependency_wait), -1, error);
      ASSERT_TRUE(request.retain(observer).has_value());
      ASSERT_TRUE(request.release(dependency_wait));

      auto decision = request.take_notification();
      ASSERT_TRUE(decision.has_value());
      EXPECT_TRUE(*decision == Decision::Activate);
      expect_error(request.take_notification(), EINVAL);
      expect_error(request.take_notification(true), EINVAL);
      if (notification_first) {
        callback.finish(*decision);
        ASSERT_TRUE(request.release_notification());
        EXPECT_FALSE(request.reclaimable());
      }
      expect_result(request.result(observer), -1, error);
      ASSERT_TRUE(request.release(observer));
      if (!notification_first) {
        EXPECT_FALSE(request.reclaimable());
        callback.finish(*decision);
        ASSERT_TRUE(request.release_notification());
      }
      EXPECT_FALSE(request.release_notification());
      EXPECT_EQ(callback.activations, 1U);
      EXPECT_EQ(callback.disposals, 0U);
      EXPECT_TRUE(request.reclaimable());
    }
  }
}

TEST(LlvmLibcAioRequestTest, InvalidPublicationAndLocalCompletionPreserveState) {
  const int invalid_errors[] = {0, -EIO, EINPROGRESS};
  Fixture fixture(true);
  Request &request = fixture.request;
  OneShot callback;
  Reference observer;
  EXPECT_FALSE(request.mark_published(ORIGINAL));
  EXPECT_FALSE(request.complete_unpublished(ECANCELED));
  EXPECT_FALSE(request.complete_withdrawn(ORIGINAL, EIO));
  ASSERT_TRUE(admit(request, callback, 0).has_value());
  ASSERT_TRUE(request.retain(observer).has_value());
  EXPECT_FALSE(request.mark_published(0));
  EXPECT_FALSE(request.mark_published(CANCEL));
  EXPECT_FALSE(request.complete(ORIGINAL, {0, 0}));
  EXPECT_FALSE(request.complete(ORIGINAL, {-1, ECANCELED}));
  EXPECT_FALSE(request.complete_withdrawn(ORIGINAL, EIO));
  for (int error : invalid_errors)
    EXPECT_FALSE(request.complete_unpublished(error));
  EXPECT_EQ(request.aio_error(), EINPROGRESS);
  expect_error(request.result(observer), EINPROGRESS);
  expect_error(request.take_notification(), EINPROGRESS);

  ASSERT_TRUE(request.mark_published(ORIGINAL));
  EXPECT_FALSE(request.mark_published(ORIGINAL));
  EXPECT_FALSE(request.mark_published(CANCEL));
  EXPECT_FALSE(request.complete_unpublished(ECANCELED));
  EXPECT_FALSE(request.complete_unpublished(EIO));
  EXPECT_FALSE(request.complete_withdrawn(0, EIO));
  EXPECT_FALSE(request.complete_withdrawn(CANCEL, EIO));
  for (int error : invalid_errors)
    EXPECT_FALSE(request.complete_withdrawn(ORIGINAL, error));
  EXPECT_EQ(request.aio_error(), EINPROGRESS);
  expect_error(request.aio_return(), EINPROGRESS);
  expect_error(request.result(observer), EINPROGRESS);
  expect_error(request.take_notification(), EINPROGRESS);
  EXPECT_FALSE(request.release_notification());
  EXPECT_TRUE(callback.suspended);
  EXPECT_FALSE(request.reclaimable());
  ASSERT_TRUE(request.complete(ORIGINAL, {6, 0}));
  EXPECT_FALSE(request.mark_published(ORIGINAL));
  EXPECT_FALSE(request.complete_withdrawn(ORIGINAL, EIO));
  EXPECT_FALSE(request.complete_unpublished(EIO));
  expect_result(request.aio_return(), 6, 0);
  expect_result(request.result(observer), 6, 0);
  ASSERT_TRUE(request.release(observer));
  auto decision = request.take_notification();
  ASSERT_TRUE(decision.has_value());
  EXPECT_TRUE(*decision == Decision::Activate);
  callback.finish(*decision);
  ASSERT_TRUE(request.release_notification());
  EXPECT_EQ(callback.activations, 1U);
  EXPECT_TRUE(request.reclaimable());
}

TEST(LlvmLibcAioRequestTest, PublicationAndQueuedCancellationAreSerialized) {
  for (bool publication_first : BOTH_ORDERS) {
    Fixture fixture(false, 1);
    Request &request = fixture.request;
    Cancel cancel(CANCEL);
    ASSERT_TRUE(request.accept().has_value());
    ASSERT_TRUE(request.prepare_cancel(cancel).has_value());
    EXPECT_FALSE(request.publish_cancel(cancel));
    if (publication_first) {
      ASSERT_TRUE(request.mark_published(ORIGINAL));
      EXPECT_FALSE(request.complete_unpublished(ECANCELED));
      ASSERT_TRUE(request.publish_cancel(cancel));
      ASSERT_TRUE(request.complete_cancel(cancel, CANCEL, 0));
      EXPECT_EQ(request.aio_error(), EINPROGRESS);
      expect_error(request.aio_return(), EINPROGRESS);
      EXPECT_FALSE(request.complete_unpublished(ECANCELED));
      ASSERT_TRUE(request.release_cancel(cancel));
      EXPECT_FALSE(request.reclaimable());
      // A successful cancel CQE does not determine the original's result.
      ASSERT_TRUE(request.complete(ORIGINAL, {4, 0}));
      expect_result(request.aio_return(), 4, 0);
    } else {
      ASSERT_TRUE(request.complete_unpublished(ECANCELED));
      EXPECT_FALSE(request.mark_published(ORIGINAL));
      EXPECT_FALSE(request.publish_cancel(cancel));
      EXPECT_FALSE(request.complete(ORIGINAL, {4, 0}));
      expect_result(request.aio_return(), -1, ECANCELED);
      EXPECT_FALSE(request.reclaimable());
      expect_result(request.original_result(cancel), -1, ECANCELED);
      ASSERT_TRUE(request.abort_cancel(cancel));
    }
    EXPECT_TRUE(request.reclaimable());
  }
}

TEST(LlvmLibcAioRequestTest, WithdrawalRetainsNotificationAndCancelOwners) {
  const int errors[] = {ECANCELED, EIO};
  for (int error : errors) {
    for (bool cancel_first : BOTH_ORDERS) {
      Fixture fixture(true, 3);
      Request &request = fixture.request;
      OneShot callback;
      Reference observer;
      Cancel published(CANCEL), prepared(CANCEL + 1), late(CANCEL + 2);
      ASSERT_TRUE(admit(request, callback, 0).has_value());
      ASSERT_TRUE(request.retain(observer).has_value());
      ASSERT_TRUE(request.prepare_cancel(published).has_value());
      ASSERT_TRUE(request.prepare_cancel(prepared).has_value());
      ASSERT_TRUE(request.mark_published(ORIGINAL));
      ASSERT_TRUE(request.publish_cancel(published));
      if (cancel_first) {
        ASSERT_TRUE(request.complete_cancel(published, CANCEL, -ENOENT));
        EXPECT_EQ(request.aio_error(), EINPROGRESS);
        expect_error(request.take_notification(), EINPROGRESS);
      }
      // Model independent transport withdrawal proof, not a cancel CQE.
      ASSERT_TRUE(request.complete_withdrawn(ORIGINAL, error));
      EXPECT_EQ(request.aio_error(), error);
      EXPECT_FALSE(request.complete_withdrawn(ORIGINAL, EINVAL));
      EXPECT_FALSE(request.complete_unpublished(EINVAL));
      EXPECT_FALSE(request.mark_published(ORIGINAL));
      EXPECT_FALSE(request.complete(ORIGINAL, {1, 0}));
      EXPECT_FALSE(request.publish_cancel(prepared));
      EXPECT_FALSE(request.complete_cancel(prepared, prepared.token(), 0));
      expect_error(request.prepare_cancel(late), EINVAL);
      expect_result(request.aio_return(), -1, error);
      expect_result(request.result(observer), -1, error);
      expect_result(request.original_result(prepared), -1, error);
      ASSERT_TRUE(request.release(observer));
      ASSERT_TRUE(request.abort_cancel(prepared));
      EXPECT_FALSE(request.abort_cancel(published));
      if (!cancel_first) {
        EXPECT_FALSE(request.release_cancel(published));
        ASSERT_TRUE(request.complete_cancel(published, CANCEL, -ENOENT));
      }
      auto cancel_result = request.cancel_result(published);
      ASSERT_TRUE(cancel_result.has_value());
      EXPECT_EQ(*cancel_result, -ENOENT);
      expect_result(request.original_result(published), -1, error);
      ASSERT_TRUE(request.release_cancel(published));
      EXPECT_FALSE(request.reclaimable());
      auto decision = request.take_notification();
      ASSERT_TRUE(decision.has_value());
      EXPECT_TRUE(*decision == Decision::Activate);
      expect_error(request.take_notification(), EINVAL);
      expect_error(request.take_notification(true), EINVAL);
      EXPECT_FALSE(request.reclaimable());
      callback.finish(*decision);
      ASSERT_TRUE(request.release_notification());
      EXPECT_FALSE(request.release_notification());
      EXPECT_EQ(callback.activations, 1U);
      EXPECT_EQ(callback.disposals, 0U);
      EXPECT_TRUE(request.reclaimable());
    }
  }
}

TEST(LlvmLibcAioRequestTest, LocalCompletionPreservesTeardownSuppression) {
  for (bool withdrawn : BOTH_ORDERS) {
    for (bool completion_first : BOTH_ORDERS) {
      Fixture fixture(true, 0);
      Request &request = fixture.request;
      OneShot callback;
      ASSERT_TRUE(admit(request, callback, 0).has_value());
      if (withdrawn)
        ASSERT_TRUE(request.mark_published(ORIGINAL));
      if (completion_first) {
        ASSERT_TRUE(withdrawn ? request.complete_withdrawn(ORIGINAL, ECANCELED)
                              : request.complete_unpublished(ECANCELED));
      }
      auto decision = request.take_notification(true);
      ASSERT_TRUE(decision.has_value());
      EXPECT_TRUE(*decision == Decision::Suppress);
      callback.finish(*decision);
      ASSERT_TRUE(request.release_notification());
      EXPECT_FALSE(request.reclaimable());
      if (!completion_first) {
        EXPECT_EQ(request.aio_error(), EINPROGRESS);
        ASSERT_TRUE(withdrawn ? request.complete_withdrawn(ORIGINAL, ECANCELED)
                              : request.complete_unpublished(ECANCELED));
      }
      expect_error(request.take_notification(), EINVAL);
      expect_error(request.take_notification(true), EINVAL);
      EXPECT_FALSE(request.release_notification());
      expect_result(request.aio_return(), -1, ECANCELED);
      EXPECT_EQ(callback.activations, 0U);
      EXPECT_EQ(callback.disposals, 1U);
      EXPECT_TRUE(request.reclaimable());
    }
  }
}

TEST(LlvmLibcAioRequestTest, OriginalBeforeCancelCompletionRetainsControl) {
  Fixture fixture(true);
  Request &request = fixture.request;
  OneShot callback;
  Cancel cancel(CANCEL);
  ASSERT_TRUE(admit(request, callback, 0).has_value());
  ASSERT_TRUE(request.prepare_cancel(cancel).has_value());
  ASSERT_TRUE(request.mark_published(ORIGINAL));
  ASSERT_TRUE(request.publish_cancel(cancel));
  ASSERT_TRUE(request.complete(ORIGINAL, {-1, ECANCELED}));
  EXPECT_EQ(request.aio_error(), ECANCELED);
  auto decision = request.take_notification();
  ASSERT_TRUE(decision.has_value());
  EXPECT_TRUE(*decision == Decision::Activate);
  callback.finish(*decision);
  ASSERT_TRUE(request.release_notification());
  expect_result(request.aio_return(), -1, ECANCELED);
  expect_error(request.aio_return(), EINVAL);
  expect_error(request.cancel_result(cancel), EINPROGRESS);
  expect_result(request.original_result(cancel), -1, ECANCELED);
  EXPECT_FALSE(request.abort_cancel(cancel));
  EXPECT_FALSE(request.release_cancel(cancel));
  EXPECT_FALSE(cancel.reclaimable());
  EXPECT_FALSE(request.reclaimable());
  ASSERT_TRUE(request.complete_cancel(cancel, CANCEL, 0));
  EXPECT_FALSE(request.complete_cancel(cancel, CANCEL, -ENOENT));
  auto result = request.cancel_result(cancel);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 0);
  EXPECT_FALSE(request.reclaimable());
  expect_error(request.take_notification(), EINVAL);
  EXPECT_EQ(callback.activations, 1U);
  ASSERT_TRUE(request.release_cancel(cancel));
  EXPECT_FALSE(request.release_cancel(cancel));
  expect_error(request.original_result(cancel), EINVAL);
  EXPECT_TRUE(cancel.reclaimable());
  EXPECT_TRUE(request.reclaimable());
}

TEST(LlvmLibcAioRequestTest, CancelCompletionBeforeOriginalRetainsBuffers) {
  Fixture fixture(true);
  Request &request = fixture.request;
  OneShot callback;
  Cancel cancel(CANCEL);
  ASSERT_TRUE(admit(request, callback, 0).has_value());
  ASSERT_TRUE(request.prepare_cancel(cancel).has_value());
  ASSERT_TRUE(request.mark_published(ORIGINAL));
  ASSERT_TRUE(request.publish_cancel(cancel));
  ASSERT_TRUE(request.complete_cancel(cancel, CANCEL, 0));
  EXPECT_EQ(request.aio_error(), EINPROGRESS);
  expect_error(request.original_result(cancel), EINPROGRESS);
  expect_error(request.aio_return(), EINPROGRESS);
  expect_error(request.take_notification(), EINPROGRESS);
  EXPECT_FALSE(request.release_notification());
  ASSERT_TRUE(request.release_cancel(cancel));
  EXPECT_TRUE(cancel.reclaimable());
  EXPECT_FALSE(request.reclaimable());
  EXPECT_TRUE(callback.suspended);
  EXPECT_EQ(callback.activations, 0U);
  ASSERT_TRUE(request.complete(ORIGINAL, {-1, ECANCELED}));
  expect_result(request.aio_return(), -1, ECANCELED);
  EXPECT_FALSE(request.reclaimable());
  auto decision = request.take_notification();
  ASSERT_TRUE(decision.has_value());
  callback.finish(*decision);
  expect_error(request.take_notification(), EINVAL);
  expect_error(request.take_notification(true), EINVAL);
  EXPECT_FALSE(request.reclaimable());
  ASSERT_TRUE(request.release_notification());
  EXPECT_FALSE(request.release_notification());
  EXPECT_EQ(callback.activations, 1U);
  EXPECT_EQ(callback.disposals, 0U);
  EXPECT_TRUE(request.reclaimable());
}

TEST(LlvmLibcAioRequestTest, CancelMissDoesNotClassifyOriginalInEitherOrder) {
  const int errors[] = {ENOENT, EALREADY};
  for (int cancel_error : errors) {
    for (bool original_first : BOTH_ORDERS) {
      Fixture fixture;
      Request &request = fixture.request;
      Cancel cancel(CANCEL);
      ASSERT_TRUE(request.accept().has_value());
      ASSERT_TRUE(request.prepare_cancel(cancel).has_value());
      ASSERT_TRUE(request.mark_published(ORIGINAL));
      ASSERT_TRUE(request.publish_cancel(cancel));
      if (original_first) {
        ASSERT_TRUE(request.complete(ORIGINAL, {9, 0}));
        expect_result(request.aio_return(), 9, 0);
      }
      ASSERT_TRUE(request.complete_cancel(cancel, CANCEL, -cancel_error));
      auto result = request.cancel_result(cancel);
      ASSERT_TRUE(result.has_value());
      EXPECT_EQ(*result, -cancel_error);
      EXPECT_FALSE(request.reclaimable());
      if (!original_first) {
        EXPECT_EQ(request.aio_error(), EINPROGRESS);
        expect_error(request.aio_return(), EINPROGRESS);
        ASSERT_TRUE(request.complete(ORIGINAL, {9, 0}));
        expect_result(request.aio_return(), 9, 0);
      }
      expect_result(request.original_result(cancel), 9, 0);
      ASSERT_TRUE(request.release_cancel(cancel));
      EXPECT_TRUE(request.reclaimable());
    }
  }
}

TEST(LlvmLibcAioRequestTest, CancelRollbackIsOnlyBeforePublication) {
  Fixture fixture;
  Request &request = fixture.request;
  Cancel zero(0), original(ORIGINAL), aborted(CANCEL), published(CANCEL + 1);
  ASSERT_TRUE(request.accept().has_value());
  expect_error(request.prepare_cancel(zero), EINVAL);
  expect_error(request.prepare_cancel(original), EINVAL);
  ASSERT_TRUE(request.prepare_cancel(aborted).has_value());
  EXPECT_FALSE(request.complete_cancel(aborted, CANCEL, 0));
  EXPECT_FALSE(request.release_cancel(aborted));
  ASSERT_TRUE(request.abort_cancel(aborted));
  EXPECT_FALSE(request.abort_cancel(aborted));
  EXPECT_FALSE(request.publish_cancel(aborted));
  expect_error(request.prepare_cancel(aborted), EINVAL);
  EXPECT_TRUE(aborted.reclaimable());
  ASSERT_TRUE(request.prepare_cancel(published).has_value());
  ASSERT_TRUE(request.mark_published(ORIGINAL));
  ASSERT_TRUE(request.publish_cancel(published));
  EXPECT_FALSE(request.publish_cancel(published));
  EXPECT_FALSE(request.abort_cancel(published));
  EXPECT_FALSE(request.release_cancel(published));
  EXPECT_FALSE(request.complete_cancel(published, ORIGINAL, 0));
  EXPECT_FALSE(request.complete_cancel(published, CANCEL, 0));
  ASSERT_TRUE(request.complete_cancel(published, published.token(), -ENOENT));
  EXPECT_FALSE(request.abort_cancel(published));
  ASSERT_TRUE(request.release_cancel(published));
  expect_error(request.cancel_result(published), EINVAL);
  expect_error(request.prepare_cancel(published), EINVAL);
  EXPECT_FALSE(request.reclaimable());
  ASSERT_TRUE(request.complete(ORIGINAL, {0, 0}));
  expect_result(request.aio_return(), 0, 0);
  EXPECT_TRUE(request.reclaimable());
}

TEST(LlvmLibcAioRequestTest, PreparedCancelCanDrainAfterOriginalRetrieval) {
  for (bool publish : BOTH_ORDERS) {
    Fixture fixture;
    Request &request = fixture.request;
    Cancel cancel(CANCEL), late(CANCEL + 1);
    ASSERT_TRUE(request.accept().has_value());
    ASSERT_TRUE(request.prepare_cancel(cancel).has_value());
    ASSERT_TRUE(request.mark_published(ORIGINAL));
    ASSERT_TRUE(request.complete(ORIGINAL, {0, 0}));
    expect_result(request.aio_return(), 0, 0);
    expect_error(request.prepare_cancel(late), EINVAL);
    EXPECT_FALSE(request.reclaimable());
    if (publish) {
      ASSERT_TRUE(request.publish_cancel(cancel));
      ASSERT_TRUE(request.complete_cancel(cancel, CANCEL, -ENOENT));
      ASSERT_TRUE(request.release_cancel(cancel));
    } else {
      ASSERT_TRUE(request.abort_cancel(cancel));
    }
    EXPECT_TRUE(request.reclaimable());
  }
}

TEST(LlvmLibcAioRequestTest, BoundedIndependentHoldersOutliveAioReturn) {
  Fixture fixture(false, 3), foreign(false, 4, CANCEL + 2);
  Request &request = fixture.request;
  Reference waiter, group, observer;
  Cancel cancel(CANCEL), excess(CANCEL + 1);
  ASSERT_TRUE(request.accept().has_value());
  ASSERT_TRUE(foreign.request.accept().has_value());
  ASSERT_TRUE(request.retain(waiter).has_value());
  ASSERT_TRUE(request.retain(group).has_value());
  ASSERT_TRUE(request.prepare_cancel(cancel).has_value());
  expect_error(request.retain(observer), EAGAIN);
  expect_error(request.prepare_cancel(excess), EAGAIN);
  EXPECT_TRUE(excess.reclaimable());
  EXPECT_EQ(observer.get(), static_cast<const Request *>(nullptr));
  expect_error(request.retain(waiter), EINVAL);
  expect_error(foreign.request.retain(waiter), EINVAL);
  EXPECT_FALSE(foreign.request.release(waiter));
  expect_error(foreign.request.result(waiter), EINVAL);
  EXPECT_FALSE(foreign.request.publish_cancel(cancel));
  EXPECT_FALSE(foreign.request.abort_cancel(cancel));
  EXPECT_FALSE(foreign.request.complete_cancel(cancel, CANCEL, 0));
  EXPECT_FALSE(foreign.request.release_cancel(cancel));
  expect_error(foreign.request.original_result(cancel), EINVAL);
  expect_error(foreign.request.cancel_result(cancel), EINVAL);
  expect_error(request.result(waiter), EINPROGRESS);
  ASSERT_TRUE(request.mark_published(ORIGINAL));
  ASSERT_TRUE(request.publish_cancel(cancel));
  ASSERT_TRUE(request.complete(ORIGINAL, {11, 0}));
  expect_result(request.aio_return(), 11, 0);
  expect_result(request.result(waiter), 11, 0);
  expect_result(request.result(group), 11, 0);
  EXPECT_FALSE(request.reclaimable());
  ASSERT_TRUE(request.release(waiter));
  EXPECT_FALSE(request.release(waiter));
  expect_error(request.result(waiter), EINVAL);
  // An existing owner may hand off to another observer after aio_return.
  ASSERT_TRUE(request.retain(observer).has_value());
  ASSERT_TRUE(request.release(group));
  ASSERT_TRUE(request.complete_cancel(cancel, CANCEL, -ENOENT));
  ASSERT_TRUE(request.release_cancel(cancel));
  EXPECT_FALSE(request.reclaimable());
  expect_result(request.result(observer), 11, 0);
  ASSERT_TRUE(request.release(observer));
  EXPECT_TRUE(request.reclaimable());
  expect_error(request.retain(observer), EINVAL);
  ASSERT_TRUE(foreign.request.mark_published(CANCEL + 2));
  ASSERT_TRUE(foreign.request.complete(CANCEL + 2, {0, 0}));
  expect_result(foreign.request.aio_return(), 0, 0);
  EXPECT_TRUE(foreign.request.reclaimable());
}

TEST(LlvmLibcAioRequestTest, MultipleCancelControlsHaveIndependentOwnership) {
  Fixture fixture(false, 2);
  Request &request = fixture.request;
  Cancel first(CANCEL), second(CANCEL + 1);
  ASSERT_TRUE(request.accept().has_value());
  ASSERT_TRUE(request.prepare_cancel(first).has_value());
  ASSERT_TRUE(request.prepare_cancel(second).has_value());
  ASSERT_TRUE(request.mark_published(ORIGINAL));
  ASSERT_TRUE(request.publish_cancel(first));
  ASSERT_TRUE(request.publish_cancel(second));
  EXPECT_FALSE(request.complete_cancel(first, second.token(), 0));
  ASSERT_TRUE(request.complete_cancel(second, second.token(), -EALREADY));
  ASSERT_TRUE(request.release_cancel(second));
  EXPECT_FALSE(request.release_cancel(second));
  ASSERT_TRUE(request.complete(ORIGINAL, {-1, ECANCELED}));
  expect_result(request.aio_return(), -1, ECANCELED);
  EXPECT_FALSE(request.reclaimable());
  ASSERT_TRUE(request.complete_cancel(first, first.token(), 0));
  ASSERT_TRUE(request.release_cancel(first));
  EXPECT_TRUE(request.reclaimable());
}

TEST(LlvmLibcAioRequestTest, ZeroHolderBudgetStillOwnsOriginalAndResult) {
  Fixture fixture(false, 0);
  Request &request = fixture.request;
  Reference reference;
  Cancel cancel(CANCEL);
  ASSERT_TRUE(request.accept().has_value());
  expect_error(request.retain(reference), EAGAIN);
  expect_error(request.prepare_cancel(cancel), EAGAIN);
  expect_error(request.take_notification(), EINVAL);
  expect_error(request.take_notification(true), EINVAL);
  EXPECT_FALSE(request.release_notification());
  EXPECT_FALSE(request.reclaimable());
  ASSERT_TRUE(request.mark_published(ORIGINAL));
  ASSERT_TRUE(request.complete(ORIGINAL, {0, 0}));
  EXPECT_FALSE(request.reclaimable());
  expect_result(request.aio_return(), 0, 0);
  EXPECT_TRUE(request.reclaimable());
}

TEST(LlvmLibcAioRequestTest, SuppressionDoesNotAbandonKernelOwnership) {
  for (bool original_first : BOTH_ORDERS) {
    Fixture fixture(true);
    Request &request = fixture.request;
    OneShot callback;
    ASSERT_TRUE(admit(request, callback, 0).has_value());
    ASSERT_TRUE(request.mark_published(ORIGINAL));
    if (original_first)
      ASSERT_TRUE(request.complete(ORIGINAL, {0, 0}));
    EXPECT_FALSE(request.release_notification());
    auto decision = request.take_notification(true);
    ASSERT_TRUE(decision.has_value());
    EXPECT_TRUE(*decision == Decision::Suppress);
    callback.finish(*decision);
    expect_error(request.take_notification(true), EINVAL);
    expect_error(request.take_notification(), EINVAL);
    ASSERT_TRUE(request.release_notification());
    EXPECT_FALSE(request.reclaimable());
    if (!original_first) {
      EXPECT_EQ(request.aio_error(), EINPROGRESS);
      expect_error(request.aio_return(), EINPROGRESS);
      ASSERT_TRUE(request.complete(ORIGINAL, {0, 0}));
    }
    expect_error(request.take_notification(), EINVAL);
    expect_result(request.aio_return(), 0, 0);
    EXPECT_EQ(callback.activations, 0U);
    EXPECT_EQ(callback.disposals, 1U);
    EXPECT_TRUE(request.reclaimable());
  }
}

TEST(LlvmLibcAioRequestTest, SuccessfulCompletionRetainsOneShotAfterAioReturn) {
  Fixture fixture(true);
  Request &request = fixture.request;
  OneShot callback;
  ASSERT_TRUE(admit(request, callback, 0).has_value());
  expect_error(request.take_notification(), EINPROGRESS);
  ASSERT_TRUE(request.mark_published(ORIGINAL));
  ASSERT_TRUE(request.complete(ORIGINAL, {5, 0}));
  expect_result(request.aio_return(), 5, 0);
  EXPECT_FALSE(request.complete(ORIGINAL, {-1, ECANCELED}));
  EXPECT_FALSE(request.reclaimable());
  auto decision = request.take_notification();
  ASSERT_TRUE(decision.has_value());
  EXPECT_TRUE(*decision == Decision::Activate);
  expect_error(request.take_notification(), EINVAL);
  expect_error(request.take_notification(true), EINVAL);
  EXPECT_FALSE(request.reclaimable());
  callback.finish(*decision);
  ASSERT_TRUE(request.release_notification());
  EXPECT_FALSE(request.release_notification());
  EXPECT_EQ(callback.activations, 1U);
  EXPECT_EQ(callback.disposals, 0U);
  EXPECT_TRUE(request.reclaimable());
}

TEST(LlvmLibcAioRequestTest, ProvenCancelWithdrawalIsNotACqe) {
  Fixture fixture(false);
  Request &request = fixture.request;
  Cancel cancel(CANCEL);
  ASSERT_TRUE(request.accept().has_value());
  ASSERT_TRUE(request.mark_published(ORIGINAL));
  ASSERT_TRUE(request.prepare_cancel(cancel).has_value());
  EXPECT_FALSE(request.withdraw_cancel(cancel, CANCEL, EIO));
  ASSERT_TRUE(request.publish_cancel(cancel));
  EXPECT_FALSE(request.withdraw_cancel(cancel, ORIGINAL, EIO));
  EXPECT_FALSE(request.withdraw_cancel(cancel, CANCEL, 0));
  EXPECT_FALSE(request.withdraw_cancel(cancel, CANCEL, EINPROGRESS));
  ASSERT_TRUE(request.withdraw_cancel(cancel, CANCEL, EIO));
  expect_error(request.cancel_result(cancel), EIO);
  expect_error(request.original_result(cancel), EINPROGRESS);
  EXPECT_FALSE(request.complete_cancel(cancel, CANCEL, 0));
  EXPECT_FALSE(request.abort_cancel(cancel));
  EXPECT_EQ(request.aio_error(), EINPROGRESS);
  ASSERT_TRUE(request.complete(ORIGINAL, {5, 0}));
  expect_result(request.aio_return(), 5, 0);
  EXPECT_FALSE(request.reclaimable());
  expect_result(request.original_result(cancel), 5, 0);
  ASSERT_TRUE(request.release_cancel(cancel));
  EXPECT_TRUE(request.reclaimable());
}
