//===-- Linux AIO submission and completion engine -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_AIO_ENGINE_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_AIO_ENGINE_H

#include "src/__support/CPP/new.h"
#include "src/__support/OSUtil/linux/aio_completion_queue.h"
#include "src/__support/OSUtil/linux/aio_submission_resources.h"
#include "src/__support/OSUtil/linux/stat/kernel_statx_types.h"
#include "src/__support/threads/aio_file_dependencies.h"
#include "src/__support/threads/aio_request.h"

namespace LIBC_NAMESPACE_DECL {

// The process service serializes ALL calls, masks application signals and
// cancellation while holding its gate, and keeps this engine, ring and request
// storage stable. Capture resources on the submitting thread before admit();
// only the service worker calls progress(), never the public submission path.
// No application callbacks, allocation, descriptor capture or waiting here.
//
// Identity qualification is a nonblocking, non-reentrant service policy, not
// an assumption that statx success guarantees globally unique inode numbers.
// Rejected/unknown identities fail dependent syncs, not ordinary transfers.
template <class RingLike = io_uring::Ring<>,
          class ResourceOperations = aio_submission_resources::LinuxOperations>
class AioEngine {
  using Queue = AioCompletionQueue<RingLike>;
  using Dependencies = AioFileDependencies;

public:
  enum class Operation { Read, Write, Sync, DataSync };
  using Classify = ErrorOr<Dependencies::Identity> (*)(
      void *, int, const internal::kernel_statx_buf &);
  // Internal wake publication only: runs under the service gate after the
  // original result, before any result consumption/reuse. No allocation,
  // application code, engine mutation/reentry or blocking is permitted.
  using Completion = void (*)(void *, const void *);

  struct Input {
    const void *control_block;
    int descriptor;
    Operation operation;
    void *buffer;
    uint32_t size;
    int64_t offset;
    bool notify;
  };

  class Request;
  class Cancel;

private:
  struct Work {
    typename Queue::Entry entry;
    Request *request = nullptr;
    Cancel *cancel = nullptr;
    Work *previous = nullptr;
    bool metadata = false;
  };

public:
  class Request {
    friend class AioEngine;
    AioEngine *owner = nullptr;
    Request *next = nullptr;
    Input input{};
    Dependencies::Record dependency;
    Work original;
    Work metadata;
    internal::kernel_statx_buf stat{};
    alignas(AioRequest) unsigned char state_storage[sizeof(AioRequest)];
    AioRequest *state_ptr = nullptr;
    bool bound = false;
    bool terminal = false;
    bool issued = false;
    bool metadata_issued = false;
    bool metadata_done = false;

  public:
    AioSubmissionResources<RingLike, ResourceOperations> resources;
    LIBC_INLINE explicit Request(RingLike &ring, ResourceOperations ops = {})
        : resources(ring, ops) {}
    Request(const Request &) = delete;
    Request &operator=(const Request &) = delete;
    LIBC_INLINE ~Request() {
      if (owner)
        __builtin_trap();
      if (state_ptr)
        state_ptr->~AioRequest();
    }
    // Only while the caller holds the process service gate. References and
    // notification ownership must be released before retire() can succeed.
    LIBC_INLINE AioRequest &state() { return *state_ptr; }
    LIBC_INLINE bool complete() const { return terminal; }
    LIBC_INLINE bool published() const { return issued; }
  };

  class Cancel {
    friend class AioEngine;
    AioEngine *owner = nullptr;
    Cancel *next = nullptr;
    Work work;
    alignas(AioRequest::CancelControl) unsigned char storage[sizeof(
        AioRequest::CancelControl)];
    AioRequest::CancelControl *control = nullptr;
    bool issued = false;
    bool terminal = false;
    int local_error = 0;

  public:
    LIBC_INLINE Cancel() = default;
    Cancel(const Cancel &) = delete;
    Cancel &operator=(const Cancel &) = delete;
    LIBC_INLINE ~Cancel() {
      if (owner)
        __builtin_trap();
      if (control)
        control->~CancelControl();
    }
    LIBC_INLINE bool complete() const { return terminal; }
  };

private:
  RingLike &ring;
  Queue queue;
  Dependencies dependencies;
  const uint64_t process;
  Classify classify;
  void *classification_context;
  Completion completion;
  void *completion_context;
  Request *requests = nullptr;
  Request *tail = nullptr;
  Work *published = nullptr;
  Cancel *controls = nullptr;
  bool submitting = false;
  Request *cleaning = nullptr;

  [[noreturn]] static void fatal() {
    (void)LIBC_NAMESPACE::syscall_impl<long>(SYS_exit_group, 127);
    __builtin_trap();
  }
  LIBC_INLINE static bool sync(Operation op) {
    return op == Operation::Sync || op == Operation::DataSync;
  }
  LIBC_INLINE void unlink_publication(Work &work) {
    Work **link = &published;
    while (*link && *link != &work)
      link = &(*link)->previous;
    if (!*link)
      fatal();
    *link = work.previous;
    work.previous = nullptr;
  }
  LIBC_INLINE void finish(Request &request, int result, bool withdrawn) {
    auto &state = request.state();
    bool completed =
        request.issued
            ? (withdrawn
                   ? state.complete_withdrawn(state.identity().token, -result)
                   : state.complete(
                         state.identity().token,
                         {result < 0 ? -1 : result, result < 0 ? -result : 0}))
            : state.complete_unpublished(-result);
    if (!completed)
      fatal();
    auto &dep = request.dependency;
    int error = result < 0 ? -result : 0;
    if (sync(request.input.operation) && !dep.decision_final()) {
      if (!dependencies.abandon_sync(dep, dep.order(), error))
        fatal();
    } else if (!dependencies.complete(dep, dep.order(), error)) {
      fatal();
    }
    request.terminal = true;
    if (completion)
      completion(completion_context, request.input.control_block);
  }
  LIBC_INLINE void metadata_result(Request &request, int result) {
    auto &dep = request.dependency;
    auto identity =
        result < 0 ? ErrorOr<Dependencies::Identity>(Error(-result))
                   : classify(classification_context,
                              request.resources.descriptor(), request.stat);
    if (identity) {
      if (!dependencies.set_known(dep, dep.order(), *identity))
        fatal();
    } else if (!dependencies.set_unknown(dep, dep.order(), identity.error())) {
      fatal();
    }
    request.metadata_done = true;
  }
  LIBC_INLINE ErrorOr<void> publish(Work &work) {
    Request &request = *work.request;
    io_uring_sqe sqe{};
    sqe.flags = IOSQE_ASYNC;
    sqe.fd = request.resources.descriptor();
    sqe.personality = static_cast<uint16_t>(request.resources.personality());
    sqe.user_data = work.entry.token();
    if (work.cancel) {
      sqe.opcode = IORING_OP_ASYNC_CANCEL;
      sqe.fd = -1;
      sqe.addr = request.state().identity().token;
    } else if (work.metadata) {
      sqe.opcode = IORING_OP_STATX;
      sqe.addr = reinterpret_cast<uintptr_t>("");
      sqe.off = reinterpret_cast<uintptr_t>(&request.stat);
      sqe.len = internal::KERNEL_STATX_BASIC_STATS_MASK;
      sqe.statx_flags = AT_EMPTY_PATH;
    } else if (sync(request.input.operation)) {
      sqe.opcode = IORING_OP_FSYNC;
      sqe.fsync_flags = request.input.operation == Operation::DataSync
                            ? IORING_FSYNC_DATASYNC
                            : 0;
    } else {
      sqe.opcode = request.input.operation == Operation::Read ? IORING_OP_READ
                                                              : IORING_OP_WRITE;
      sqe.addr = reinterpret_cast<uintptr_t>(request.input.buffer);
      sqe.len = request.input.size;
      sqe.off = uint64_t(request.input.offset);
    }
    auto result = ring.enqueue(sqe);
    if (!result)
      return Error(result.error());
    queue.publish(work.entry);
    if (work.cancel) {
      if (!request.state().publish_cancel(*work.cancel->control))
        fatal();
      work.cancel->issued = true;
    } else if (work.metadata)
      request.metadata_issued = true;
    else {
      if (!request.state().mark_published(sqe.user_data))
        fatal();
      request.issued = true;
    }
    work.previous = published;
    published = &work;
    return {};
  }
  LIBC_INLINE void enqueue_failure(Work &work, int error) {
    if (!queue.rollback(work.entry))
      fatal();
    if (work.cancel) {
      if (!work.request->state().abort_cancel(*work.cancel->control))
        fatal();
      work.cancel->local_error = error;
      work.cancel->terminal = true;
    } else if (work.metadata)
      metadata_result(*work.request, -error);
    else
      finish(*work.request, -error, false);
  }
  LIBC_INLINE void withdraw_tail(int error) {
    // The list is in reverse publication order, not admission order. An entry
    // consumed by the kernel remains owned until its real CQE arrives.
    while (published) {
      Work &work = *published;
      auto result = queue.withdraw(work.entry);
      if (!result) {
        if (result.error() != EAGAIN)
          fatal();
        return;
      }
      uint64_t token = work.cancel ? work.cancel->control->token() : 0;
      unlink_publication(work);
      if (work.cancel) {
        if (!work.request->state().withdraw_cancel(*work.cancel->control, token,
                                                   error))
          fatal();
        work.cancel->terminal = true;
      } else if (work.metadata)
        metadata_result(*work.request, -error);
      else
        finish(*work.request, -error, true);
    }
  }

public:
  LIBC_INLINE
  AioEngine(RingLike &ring, uint64_t &watermark, uint64_t process,
            uint32_t limit, Classify classify, void *context = nullptr,
            Completion completion = nullptr, void *completion_context = nullptr)
      : ring(ring), queue(ring, watermark), dependencies(limit),
        process(process), classify(classify), classification_context(context),
        completion(completion), completion_context(completion_context) {}
  AioEngine(const AioEngine &) = delete;
  AioEngine &operator=(const AioEngine &) = delete;
  LIBC_INLINE ~AioEngine() {
    if (requests || published || controls || submitting || cleaning)
      fatal();
  }
  LIBC_INLINE ErrorOr<void> initialize(uint32_t capacity) {
    if (!process || !classify || capacity < 3)
      return Error(EINVAL);
    return queue.initialize(capacity);
  }
  LIBC_INLINE bool empty() const {
    return requests == nullptr && controls == nullptr && !submitting;
  }
  LIBC_INLINE Request *find(const void *control_block) {
    for (auto *request = requests; request; request = request->next)
      if (request->bound && request->input.control_block == control_block)
        return request;
    return nullptr;
  }
  // Under the service gate. The visitor may retain state references but must
  // not change engine bindings, allocate, call application code or reenter.
  // Selection is the original submission descriptor, not its private dup.
  template <class Visitor>
  LIBC_INLINE void visit_bound(int descriptor, Visitor visitor) {
    for (auto *request = requests; request; request = request->next)
      if (request->bound && request->input.descriptor == descriptor)
        visitor(*request);
  }
  LIBC_INLINE ErrorOr<int> error(const void *control_block) {
    auto *request = find(control_block);
    if (!request)
      return Error(EINVAL);
    return request->state().aio_error();
  }
  LIBC_INLINE ErrorOr<AioRequest::Result>
  take_result(const void *control_block) {
    auto *request = find(control_block);
    if (!request)
      return Error(EINVAL);
    auto result = request->state().aio_return();
    if (result)
      request->bound = false;
    return result;
  }

  // All notification preparation must already have succeeded. On failure the
  // service still owns resources/callback cleanup; admit never consumes them.
  // Two credits per request (identity and original), plus control headroom.
  LIBC_INLINE ErrorOr<void> admit(Request &request, Input input) {
    if (request.owner || request.state_ptr || !request.resources.ready() ||
        !input.control_block || input.size > INT32_MAX ||
        (input.operation != Operation::Read &&
         input.operation != Operation::Write && !sync(input.operation)))
      return Error(EINVAL);
    if (find(input.control_block))
      return Error(EBUSY);
    if (queue.available() < 3)
      return Error(EAGAIN);
    auto token = queue.prepare(
        request.original.entry,
        sync(input.operation) ? Queue::Kind::Sync : Queue::Kind::Transfer,
        &request.original, sync(input.operation) ? 0 : input.size);
    if (!token)
      return Error(token.error());
    auto metadata = queue.prepare(request.metadata.entry, Queue::Kind::Metadata,
                                  &request.metadata, 0);
    if (!metadata) {
      queue.rollback(request.original.entry);
      return Error(metadata.error());
    }
    auto order = dependencies.admit(request.dependency,
                                    sync(input.operation)
                                        ? Dependencies::Kind::Sync
                                        : Dependencies::Kind::Operation);
    if (!order) {
      queue.rollback(request.metadata.entry);
      queue.rollback(request.original.entry);
      return Error(order.error());
    }
    request.state_ptr = new (request.state_storage) AioRequest(
        {input.control_block, process, *token}, input.notify, UINT32_MAX);
    if (!request.state().accept())
      fatal();
    request.owner = this;
    request.bound = true;
    request.input = input;
    request.original.request = request.metadata.request = &request;
    request.metadata.metadata = true;
    if (tail)
      tail->next = &request;
    else
      requests = &request;
    tail = &request;
    return {};
  }

  // Only guarantees the unpublished part of aio_cancel. Published work needs
  // a separate tracked cancellation SQE; false is NOT an aggregate result.
  LIBC_INLINE bool cancel_queued(Request &request) {
    if (request.owner != this || request.terminal || request.issued)
      return false;
    if (!queue.rollback(request.original.entry))
      fatal();
    finish(request, -ECANCELED, false);
    if (!request.metadata_issued && !request.metadata_done) {
      if (!queue.rollback(request.metadata.entry))
        fatal();
      metadata_result(request, -ECANCELED);
    }
    return true;
  }

  // Preparation reserves a control credit without enqueueing. The worker
  // publishes later, so callers never run a possibly blocking enter syscall.
  LIBC_INLINE ErrorOr<void> cancel_published(Request &request, Cancel &cancel) {
    if (request.owner != this || !request.issued || request.terminal ||
        cancel.owner || cancel.control)
      return Error(EINVAL);
    auto token =
        queue.prepare(cancel.work.entry, Queue::Kind::Cancel, &cancel.work, 0);
    if (!token)
      return Error(token.error());
    cancel.control = new (cancel.storage) AioRequest::CancelControl(*token);
    auto result = request.state().prepare_cancel(*cancel.control);
    if (!result) {
      queue.rollback(cancel.work.entry);
      return Error(result.error());
    }
    cancel.owner = this;
    cancel.work.cancel = &cancel;
    cancel.work.request = &request;
    cancel.next = controls;
    controls = &cancel;
    return {};
  }
  LIBC_INLINE ErrorOr<int> cancel_result(const Cancel &cancel) {
    if (cancel.owner != this)
      return Error(EINVAL);
    if (cancel.local_error)
      return Error(cancel.local_error);
    return cancel.work.request->state().cancel_result(*cancel.control);
  }
  LIBC_INLINE ErrorOr<AioRequest::Result>
  original_result(const Cancel &cancel) {
    if (cancel.owner != this || cancel.local_error)
      return Error(EINVAL);
    return cancel.work.request->state().original_result(*cancel.control);
  }
  LIBC_INLINE bool retire_cancel(Cancel &cancel) {
    if (cancel.owner != this || !cancel.terminal ||
        !cancel.work.entry.reclaimable())
      return false;
    if (!cancel.local_error &&
        !cancel.work.request->state().release_cancel(*cancel.control))
      fatal();
    Cancel **link = &controls;
    while (*link != &cancel)
      link = &(*link)->next;
    *link = cancel.next;
    cancel.next = nullptr;
    cancel.owner = nullptr;
    return true;
  }

  // Stage one worker iteration while holding the service gate. The service
  // then releases the gate, calls ring.submit(), reacquires the gate and calls
  // finish_submit(). No other enqueue/poll/withdraw/submit/reset is permitted
  // between those calls. Status/result queries and local queued cancellation
  // remain usable, including from handlers, while enter is in progress.
  // Resource capture/release must also be excluded during that interval.
  LIBC_INLINE int stage() {
    if (submitting || cleaning)
      return EBUSY;
    int error = stage_work();
    if (!error)
      submitting = true;
    return error;
  }
  LIBC_INLINE int finish_submit(ErrorOr<unsigned> submitted) {
    if (!submitting)
      fatal();
    submitting = false;
    if (!submitted) {
      withdraw_tail(submitted.error());
      return submitted.error();
    }
    return 0;
  }
  LIBC_INLINE bool submit_in_progress() const { return submitting; }

  // Convenience for an exclusively owned engine (including isolated tests).
  // A process service with public queries MUST use the split interface above.
  LIBC_INLINE int progress() {
    int error = stage();
    return error ? error : finish_submit(ring.submit());
  }

private:
  // Public submission never calls this function.
  // Short submit counts do not imply failure and never republish work. Any
  // healthy submit error retracts only proven unconsumed tail entries; consumed
  // entries retain their original CQE ownership. Return values are diagnostics,
  // not permission to free requests or shut down a nonempty ring.
  LIBC_INLINE int stage_work() {
    for (;;) {
      auto result = queue.poll();
      if (!result) {
        if (result.error() != EAGAIN)
          return result.error();
        break;
      }
      auto &work = *static_cast<Work *>(result->context);
      unlink_publication(work);
      if (work.cancel) {
        if (!work.request->state().complete_cancel(
                *work.cancel->control, result->token, result->raw_result))
          fatal();
        work.cancel->terminal = true;
      } else if (work.metadata)
        metadata_result(*work.request, result->raw_result);
      else
        finish(*work.request, result->raw_result, false);
    }
    for (auto *cancel = controls; cancel; cancel = cancel->next) {
      if (cancel->issued || cancel->terminal)
        continue;
      // A previously published original may have been withdrawn while this
      // control was waiting. Nothing remains for the kernel to cancel.
      if (cancel->work.request->terminal &&
          cancel->work.request->state().aio_error() != EINPROGRESS) {
        enqueue_failure(cancel->work, EALREADY);
        continue;
      }
      auto result = publish(cancel->work);
      if (!result && result.error() != EAGAIN)
        enqueue_failure(cancel->work, result.error());
    }
    for (auto *request = requests; request; request = request->next) {
      if (!request->metadata_issued && !request->metadata_done) {
        auto result = publish(request->metadata);
        if (!result && result.error() != EAGAIN)
          enqueue_failure(request->metadata, result.error());
      }
      if (request->terminal || request->issued)
        continue;
      if (sync(request->input.operation) &&
          !request->dependency.decision_final()) {
        auto decision = dependencies.decide(request->dependency,
                                            request->dependency.order());
        if (!decision) {
          enqueue_failure(request->original, decision.error());
          continue;
        }
        if (*decision == Dependencies::Decision::Wait)
          continue;
      }
      auto result = publish(request->original);
      if (!result && result.error() != EAGAIN)
        enqueue_failure(request->original, result.error());
    }
    return 0;
  }

public:
  // Begin under the service gate. No kernel users, application result,
  // observer or notification owner may remain. Then release the gate and call
  // request.resources.release() under exclusive ring ownership. Queries,
  // already-captured admission and queued cancellation remain available, but
  // no submit, capture, other release, reset or destruction may intervene.
  // This keeps resource syscalls out of the signal-safe short state lock.
  LIBC_INLINE bool begin_retire(Request &request) {
    if (submitting || cleaning || request.owner != this ||
        !request.state().reclaimable() ||
        !request.original.entry.reclaimable() ||
        !request.metadata.entry.reclaimable())
      return false;
    auto &dep = request.dependency;
    if (!dep.reclaimable() && !dependencies.release(dep, dep.order()))
      return false;
    cleaning = &request;
    return true;
  }

  // Reacquire the service gate after the resource-release attempt. Failed
  // unregister retains the record for another begin/release/finish attempt;
  // close consumes the descriptor even on error and must never be retried.
  // A true result permits destruction outside the gate. Never free here.
  LIBC_INLINE bool finish_retire(Request &request) {
    if (cleaning != &request || request.owner != this)
      fatal();
    cleaning = nullptr;
    if (!request.resources.drained())
      return false;
    Request **link = &requests;
    Request *previous = nullptr;
    while (*link != &request) {
      previous = *link;
      link = &(*link)->next;
    }
    *link = request.next;
    if (tail == &request)
      tail = previous;
    request.next = nullptr;
    request.owner = nullptr;
    return true;
  }

  LIBC_INLINE bool cleanup_in_progress() const { return cleaning != nullptr; }

  // Convenience for an exclusively owned engine, not for a service holding
  // its signal-safe state lock. That service must use the split interface.
  LIBC_INLINE bool retire(Request &request) {
    if (!begin_retire(request))
      return false;
    (void)request.resources.release();
    return finish_retire(request);
  }
};

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_AIO_ENGINE_H
