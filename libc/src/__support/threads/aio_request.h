//===-- Private AIO request ownership --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_AIO_REQUEST_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_AIO_REQUEST_H

#include "hdr/errno_macros.h"
#include "hdr/stdint_proxy.h"
#include "hdr/types/ssize_t.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

// Caller-serialized, single-use request state, not a public AIO provider. All
// access (including queries) requires the service lock. No allocation, atomics,
// callback invocation, token lookup or automatic release is performed here.
//
// Preconditions for the integrating service:
// - Bind the aiocb address, process incarnation and non-reused nonzero token in
//   trusted storage before acceptance. Never dereference an unvalidated aiocb
//   private pointer. Validate the process and route CQEs through that binding;
//   matching a numeric token here does not authenticate a caller or process.
// - Capture inputs and descriptor ownership, reserve CQ capacity, and finish
//   all fallible preparation before accept(). THREAD notification requires a
//   precreated, suspended one-shot callback, not a late-launch/drop policy.
//   reject() is only for admission failure before kernel publication; the
//   caller must dispose of any suspended callback without running it.
// - accept() reserves original-operation ownership before kernel publication.
//   Serialize mark_published() and transport publication against local queued
//   completion. Only an original CQE or proven local completion, never a cancel
//   CQE, ends that ownership. Wake waiters after any completion path succeeds.
//   No multishot/skip-success CQEs.
// - Keep this object and its associated storage alive until reclaimable().
//   Keep attached Reference and CancelControl objects alive until explicit
//   release/abort; destroying a holder does not release it. Each cancel token
//   is independently bound and globally non-reused in the service namespace.
//
// Request identity cannot be reset. Additional holders (waiters, list groups,
// result observers and cancel controls) share an explicit per-request bound.
// This is not a ring-wide CQ bound, submission engine, cancellation aggregate,
// notification service, fork/exec implementation or full POSIX contract.
class AioRequest {
public:
  struct Identity {
    const void *control_block;
    uint64_t process;
    uint64_t token;
  };

  // Outer ErrorOr errors describe invalid/early retrieval; Result::error is
  // the original operation's errno, with value == -1 on operation failure.
  struct Result {
    ssize_t value;
    int error;
  };

  enum class NotificationDecision { Activate, Suppress };

  class Reference {
    friend class AioRequest;
    AioRequest *owner = nullptr;

  public:
    LIBC_INLINE constexpr Reference() = default;
    Reference(const Reference &) = delete;
    Reference &operator=(const Reference &) = delete;
    LIBC_INLINE const AioRequest *get() const { return owner; }
  };

  class CancelControl {
    friend class AioRequest;
    enum class State { Fresh, Prepared, Published, Complete, Withdrawn, Retired };
    const uint64_t token_value;
    AioRequest *owner = nullptr;
    State state = State::Fresh;
    int cqe_result = 0;
    int submission_error = 0;

  public:
    LIBC_INLINE explicit constexpr CancelControl(uint64_t token)
        : token_value(token) {}
    CancelControl(const CancelControl &) = delete;
    CancelControl &operator=(const CancelControl &) = delete;
    LIBC_INLINE uint64_t token() const { return token_value; }
    LIBC_INLINE bool reclaimable() const { return owner == nullptr; }
  };

private:
  enum class State { Preparing, Rejected, Pending, Complete };
  enum class Original { Unpublished, Published, Completed, Withdrawn };
  enum class Notification { None, Waiting, Claimed, Released };
  const Identity identity_value;
  const uint32_t holder_limit;
  const bool wants_notification;
  State state = State::Preparing;
  Original original = Original::Unpublished;
  Notification notification = Notification::None;
  bool result_owned = false;
  uint32_t holders = 0;
  Result completion = {-1, EINPROGRESS};

  LIBC_INLINE bool accepted() const {
    return state == State::Pending || state == State::Complete;
  }

  LIBC_INLINE ErrorOr<Result> completed_result() const {
    if (state != State::Complete)
      return Error(EINPROGRESS);
    return completion;
  }

public:
  LIBC_INLINE constexpr AioRequest(Identity identity, bool notify,
                                   uint32_t additional_holder_limit)
      : identity_value(identity), holder_limit(additional_holder_limit),
        wants_notification(notify) {}
  AioRequest(const AioRequest &) = delete;
  AioRequest &operator=(const AioRequest &) = delete;

  LIBC_INLINE const Identity &identity() const { return identity_value; }

  LIBC_INLINE bool reclaimable() const {
    return state == State::Rejected ||
           (state == State::Complete && !result_owned && holders == 0 &&
            (notification == Notification::None ||
             notification == Notification::Released));
  }

  LIBC_INLINE ErrorOr<void> accept() {
    if (state != State::Preparing)
      return Error(EBUSY);
    if (identity_value.control_block == nullptr ||
        identity_value.process == 0 || identity_value.token == 0)
      return Error(EINVAL);
    state = State::Pending;
    result_owned = true;
    notification =
        wants_notification ? Notification::Waiting : Notification::None;
    return {};
  }

  LIBC_INLINE bool reject() {
    if (state != State::Preparing)
      return false;
    state = State::Rejected;
    return true;
  }

  // A partially admitted lio_listio must leave a queryable status for members
  // it could not queue. Unlike reject(), this retains result ownership, but
  // creates neither an original-operation owner nor a completion notification.
  // The service disposes of any precreated callback before using this path.
  // Never use it to synthesize completion of accepted/kernel-visible work.
  LIBC_INLINE ErrorOr<void> reject_with_result(int error) {
    if (state != State::Preparing)
      return Error(EBUSY);
    if (error <= 0 || error == EINPROGRESS ||
        identity_value.control_block == nullptr ||
        identity_value.process == 0 || identity_value.token == 0)
      return Error(EINVAL);
    state = State::Complete;
    result_owned = true;
    completion = {-1, error};
    return {};
  }

  LIBC_INLINE int aio_error() const {
    if (!accepted() || !result_owned)
      return EINVAL;
    return state == State::Pending ? EINPROGRESS : completion.error;
  }

  LIBC_INLINE ErrorOr<Result> aio_return() {
    if (!accepted() || !result_owned)
      return Error(EINVAL);
    if (state == State::Pending)
      return Error(EINPROGRESS);
    result_owned = false;
    return completion;
  }

  // Call immediately after successful SQ enqueue, under the SAME service lock,
  // before CQ processing or cancellation. Failure here is a service invariant
  // violation, not permission to free published storage. Failed enqueue leaves
  // this request unpublished. Submission failure alone cannot undo publication.
  LIBC_INLINE bool mark_published(uint64_t token) {
    if (state != State::Pending || original != Original::Unpublished ||
        token != identity_value.token)
      return false;
    original = Original::Published;
    return true;
  }

  // Complete accepted queued work without a CQE. The caller must remove it
  // from all submission/dependency queues before releasing the service lock.
  LIBC_INLINE bool complete_unpublished(int error) {
    if (state != State::Pending || original != Original::Unpublished ||
        error <= 0 || error == EINPROGRESS)
      return false;
    completion = {-1, error};
    state = State::Complete;
    return true;
  }

  // Requires transport proof that the published original was withdrawn: no
  // kernel access or original CQE can still occur. A cancel CQE or submit error
  // is not proof. This class validates phase/token, not the transport proof.
  LIBC_INLINE bool complete_withdrawn(uint64_t token, int error) {
    if (state != State::Pending || original != Original::Published ||
        token != identity_value.token || error <= 0 || error == EINPROGRESS)
      return false;
    completion = {-1, error};
    original = Original::Withdrawn;
    state = State::Complete;
    return true;
  }

  // Only the original operation's terminal CQE may call this. A malformed,
  // stale or duplicate completion changes neither the result nor ownership.
  LIBC_INLINE bool complete(uint64_t token, Result result) {
    if (state != State::Pending || original != Original::Published ||
        token != identity_value.token ||
        !((result.error == 0 && result.value >= 0) ||
          (result.error > 0 && result.error != EINPROGRESS &&
           result.value == -1)))
      return false;
    completion = result;
    original = Original::Completed;
    state = State::Complete;
    return true;
  }

  LIBC_INLINE ErrorOr<void> retain(Reference &reference) {
    if (!accepted() || reclaimable() || reference.owner != nullptr)
      return Error(EINVAL);
    if (holders == holder_limit)
      return Error(EAGAIN);
    ++holders;
    reference.owner = this;
    return {};
  }

  LIBC_INLINE bool release(Reference &reference) {
    if (reference.owner != this)
      return false;
    reference.owner = nullptr;
    --holders;
    return true;
  }

  // Internal observers retain access after aio_return without reacquiring the
  // application's one-time result ownership.
  LIBC_INLINE ErrorOr<Result> result(const Reference &reference) const {
    if (reference.owner != this)
      return Error(EINVAL);
    return completed_result();
  }

  // suppress is reserved for the service's established teardown boundary. It
  // may precede original completion but never drops original-operation
  // ownership. The caller must execute the returned decision exactly once and
  // release only after callback completion or confirmed disposal of the
  // suspended callback.
  LIBC_INLINE ErrorOr<NotificationDecision>
  take_notification(bool suppress = false) {
    if (notification != Notification::Waiting)
      return Error(EINVAL);
    if (!suppress && state != State::Complete)
      return Error(EINPROGRESS);
    notification = Notification::Claimed;
    return suppress ? NotificationDecision::Suppress
                    : NotificationDecision::Activate;
  }

  LIBC_INLINE bool release_notification() {
    if (notification != Notification::Claimed)
      return false;
    notification = Notification::Released;
    return true;
  }

  // Preparation may precede original publication; it only retains a holder.
  LIBC_INLINE ErrorOr<void> prepare_cancel(CancelControl &control) {
    if (state != State::Pending ||
        control.state != CancelControl::State::Fresh ||
        control.owner != nullptr || control.token() == 0 ||
        control.token() == identity_value.token)
      return Error(EINVAL);
    if (holders == holder_limit)
      return Error(EAGAIN);
    ++holders;
    control.owner = this;
    control.state = CancelControl::State::Prepared;
    return {};
  }

  // Call immediately after successful SQ enqueue under the SAME service lock,
  // before any concurrent submit/poll. Failed enqueue leaves it Prepared.
  // After publication, a submit error alone cannot abort/release this control.
  // Prepared controls may still publish after an original CQE, but must abort
  // after local queued completion or withdrawal. Published controls always drain.
  LIBC_INLINE bool publish_cancel(CancelControl &control) {
    if (control.owner != this ||
        control.state != CancelControl::State::Prepared ||
        (original != Original::Published && original != Original::Completed))
      return false;
    control.state = CancelControl::State::Published;
    return true;
  }

  LIBC_INLINE bool abort_cancel(CancelControl &control) {
    if (control.owner != this ||
        control.state != CancelControl::State::Prepared)
      return false;
    control.state = CancelControl::State::Retired;
    control.owner = nullptr;
    --holders;
    return true;
  }

  // Preserve the raw cancel CQE result, including -ENOENT/-EALREADY. It is not
  // an AIO_CANCELED/AIO_ALLDONE/AIO_NOTCANCELED classification and cannot
  // publish ECANCELED for the original operation or activate its notification.
  LIBC_INLINE bool complete_cancel(CancelControl &control, uint64_t token,
                                   int cqe_result) {
    if (control.owner != this ||
        control.state != CancelControl::State::Published ||
        token != control.token())
      return false;
    control.cqe_result = cqe_result;
    control.state = CancelControl::State::Complete;
    return true;
  }

  // Requires the same independent transport proof as complete_withdrawn().
  // No cancel CQE will arrive. Keep the observer reference until explicit
  // release, and report a local submission error separately from a real CQE.
  LIBC_INLINE bool withdraw_cancel(CancelControl &control, uint64_t token,
                                   int error) {
    if (control.owner != this ||
        control.state != CancelControl::State::Published ||
        token != control.token() || error <= 0 || error == EINPROGRESS)
      return false;
    control.submission_error = error;
    control.state = CancelControl::State::Withdrawn;
    return true;
  }

  LIBC_INLINE ErrorOr<int> cancel_result(const CancelControl &control) const {
    if (control.owner != this)
      return Error(EINVAL);
    if (control.state == CancelControl::State::Withdrawn)
      return Error(control.submission_error);
    if (control.state != CancelControl::State::Complete)
      return Error(EINPROGRESS);
    return control.cqe_result;
  }

  LIBC_INLINE ErrorOr<Result>
  original_result(const CancelControl &control) const {
    if (control.owner != this)
      return Error(EINVAL);
    return completed_result();
  }

  LIBC_INLINE bool release_cancel(CancelControl &control) {
    if (control.owner != this ||
        (control.state != CancelControl::State::Complete &&
         control.state != CancelControl::State::Withdrawn))
      return false;
    control.state = CancelControl::State::Retired;
    control.owner = nullptr;
    --holders;
    return true;
  }
};

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_THREADS_AIO_REQUEST_H
