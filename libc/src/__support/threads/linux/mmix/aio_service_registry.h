//===-- MMIX AIO service record ownership ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_AIO_SERVICE_REGISTRY_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_AIO_SERVICE_REGISTRY_H

#include "aio_gate.h"
#include "aio_notification.h"
#include "aio_suspend_registration.h"
#include "src/__support/OSUtil/linux/aio_engine.h"
#include "src/__support/threads/aio_storage.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// Private ownership increment for an already stable service, not a public AIO
// provider. Initialize exclusively before publishing the service. Ring,
// allocator, token watermark and completion context outlive it;
// the ring is initialized separately and must never be reset while it is live.
// No engine/ring operations may bypass this registry after publication.
//
// REQUIRED EXTERNAL GUARDS: every ordinary admit/submit/maintain call holds a
// service lifetime pin, excludes fork/exec/exit transitions, and suppresses
// application signals AND cancellation for its ENTIRE duration, including
// allocation, notification preparation, lock waits and rollback. AioGate only
// protects short state regions; it does not supply these outer guards. Queries
// need the stable service lifetime but only use AioGate, never the ring lock.
// Neither preparation nor maintenance is an application-handler entrypoint.
// The credential handler must not enter this service. Destruction requires
// exclusive lifetime ownership, no calls in flight and fully drained records.
//
// Lock order is ring_operations -> gate, never the reverse. Allocation,
// notification methods and resource syscalls run OUTSIDE gate. Notification
// and allocator methods also run without the ring lock: their loader/runtime
// locks may be held by a concurrent admission caller. The separate ring lock
// excludes capture/release across the split submit/retire intervals.
// Notification methods never synchronously invoke application code; a launched
// callback may use the service on its own thread. Preparation must not wait for
// accepted I/O. Allocator follows AioStorage's concurrent detached-block
// contract.
//
// SlotsPerBlock is allocation granularity, not an admission limit. Kernel
// credits bound in-flight work, independently of retained results/rollback.
// UINT32_MAX below is the dependency counter's representational bound, not CQ
// capacity or a new resource policy. No public capability or limit is selected.
// Notification/thread admission policy remains with the external runtime.
//
// Deferred owners: public cancellation wrappers, list groups,
// public wait cancellation, lazy worker startup and process
// transition/quarantine hooks are not implemented here. In particular, do not
// use a copied service in a fork child, or destroy outstanding records to
// implement teardown.
template <class Allocator, size_t SlotsPerBlock,
          class RingLike = io_uring::Ring<>,
          class ResourceOps = aio_submission_resources::LinuxOperations,
          class Notification = AioNotification, class Gate = AioGate,
          class ControlBlock = void>
class AioServiceRegistry {
public:
  using Engine = AioEngine<RingLike, ResourceOps>;
  using Input = typename Engine::Input;
  using Wait = AioSuspendRegistration<ControlBlock, Gate>;

  // Caller-owned preparation only, never kernel-visible or used by a worker.
  // Reserve all members before starting a list; abandon all preparations if
  // that first phase fails. Each handle must be started or abandoned exactly
  // once under the ordinary external service/lifetime guards.
  class PreparedMember {
    friend class AioServiceRegistry;
    AioServiceRegistry *owner = nullptr;
    void *record = nullptr;
    Input input{};

  public:
    PreparedMember() = default;
    PreparedMember(const PreparedMember &) = delete;
    PreparedMember &operator=(const PreparedMember &) = delete;
    ~PreparedMember() {
      if (owner)
        __builtin_trap();
    }
  };

  // Stable group-owned observation, acquired in the acceptance gate epoch.
  // Its result cannot be redirected by aio_return or control-block reuse.
  // Keep the observer and service alive until release_member, including when
  // a public list waiter leaves early; do not leave it on an abandoned stack.
  class MemberObservation {
    friend class AioServiceRegistry;
    AioServiceRegistry *owner = nullptr;
    typename Engine::Request *request = nullptr;
    AioRequest::Reference reference;

  public:
    MemberObservation() = default;
    MemberObservation(const MemberObservation &) = delete;
    MemberObservation &operator=(const MemberObservation &) = delete;
    ~MemberObservation() {
      if (owner)
        __builtin_trap();
    }
  };

  // A private, single-use observation, owned by the service until release.
  // Its independent reference survives result consumption, address reuse and
  // failed cancel enqueue. Do not put it on a public caller's stack unless all
  // pending control ownership is drained before that frame can leave.
  class CancelObservation {
    friend class AioServiceRegistry;
    AioServiceRegistry *owner = nullptr;
    typename Engine::Request *request = nullptr;
    AioRequest::Reference reference;
    typename Engine::Cancel control;
    bool attempted = false;
    bool controlled = false;
    bool used = false;
    bool completed_at_capture = false;

  public:
    CancelObservation() = default;
    CancelObservation(const CancelObservation &) = delete;
    CancelObservation &operator=(const CancelObservation &) = delete;
    ~CancelObservation() {
      if (owner)
        __builtin_trap();
    }
  };

  enum class CancelStatus { AllDone, Canceled, NotCanceled };

  // Service-allocated storage with one caller (never concurrently shared).
  // finish_cancels drops that caller's access, not pending kernel ownership.
  // Only maintain() may subsequently destroy it.
  class alignas(CancelObservation) CancelBatch {
    friend class AioServiceRegistry;
    AioServiceRegistry *owner;
    CancelBatch *next = nullptr;
    size_t capacity;
    size_t count = 0;
    bool released = false;
    bool attempted = false;

    CancelBatch(AioServiceRegistry *owner, size_t capacity)
        : owner(owner), capacity(capacity) {}
    CancelObservation *items() {
      return reinterpret_cast<CancelObservation *>(this + 1);
    }

  public:
    CancelBatch(const CancelBatch &) = delete;
    CancelBatch &operator=(const CancelBatch &) = delete;
  };

private:
  enum class Phase { Preparing, Accepted, Rejected, Rollback };
  struct Record {
    typename Engine::Request request;
    Notification notification;
    Record *next = nullptr;
    // AioStorage needs the complete Record type before defining its Slot.
    void *lease;
    Phase phase = Phase::Preparing;
    bool notification_claimed = false;
    bool notification_terminal = false;
    bool notification_released = false;

    Record(RingLike &ring, ResourceOps ops, void *lease)
        : request(ring, ops), lease(lease) {}
  };
  using Storage = AioStorage<Record, SlotsPerBlock, Allocator>;
  RingLike &ring;
  Allocator &allocator;
  ResourceOps resource_ops;
  Gate gate;
  // MMIX subword atomics must own their entire containing octa.
  struct alignas(8) {
    RawMutex value;
  } ring_operations;
  AioSuspendObservers<ControlBlock> observers;
  typename Engine::Completion completion;
  void *completion_context;
  Engine engine;
  Storage storage;
  Record *records = nullptr;
  CancelBatch *cancel_batches = nullptr;
  bool maintaining = false;

  static void publish_completion(void *context, const void *control_block) {
    auto &self = *static_cast<AioServiceRegistry *>(context);
    self.observers.complete(control_block);
    if (self.completion)
      self.completion(self.completion_context, control_block);
  }

  class RingGuard {
    RawMutex &lock;

  public:
    explicit RingGuard(RawMutex &lock) : lock(lock) {
      if (!lock.lock())
        __builtin_trap();
    }
    ~RingGuard() {
      if (!lock.unlock())
        __builtin_trap();
    }
    RingGuard(const RingGuard &) = delete;
    RingGuard &operator=(const RingGuard &) = delete;
  };

  // Once resources are owned, failure to reacquire the state gate cannot be
  // reported as permission to abandon them. Queries can report entry failure.
  class StateGuard {
    typename Gate::Guard guard;

  public:
    explicit StateGuard(Gate &gate) : guard(gate) {
      if (!guard)
        __builtin_trap();
    }
  };

  LIBC_INLINE typename Storage::Slot *acquire() {
    {
      StateGuard guard(gate);
      if (auto *slot = storage.acquire())
        return slot;
    }
    auto *block = Storage::prepare(allocator);
    if (!block)
      return nullptr;
    typename Storage::Slot *slot = nullptr;
    {
      StateGuard guard(gate);
      if (storage.publish(*block))
        slot = storage.acquire();
    }
    if (!slot && !Storage::dispose(block))
      __builtin_trap();
    return slot;
  }

  LIBC_INLINE void destroy(Record &record) {
    auto *slot = static_cast<typename Storage::Slot *>(record.lease);
    {
      StateGuard guard(gate);
      Record **link = &records;
      while (*link != &record)
        link = &(*link)->next;
      *link = record.next;
    }
    record.~Record();
    {
      StateGuard guard(gate);
      if (!storage.release(*slot))
        __builtin_trap();
    }
  }

  LIBC_INLINE int begin_submit() {
    StateGuard guard(gate);
    return engine.stage();
  }
  LIBC_INLINE int finish_submit(ErrorOr<unsigned> result) {
    StateGuard guard(gate);
    return engine.finish_submit(result);
  }

  template <class Prepare>
  LIBC_INLINE ErrorOr<void>
  admit_record(Record &record, Input input, Prepare prepare,
               bool retain_failure, MemberObservation *observation = nullptr) {
    int error = 0;
    {
      RingGuard lock(ring_operations.value);
      auto captured = record.request.resources.capture(input.descriptor);
      if (!captured)
        error = captured.error();
    }
    if (!error)
      error = int(prepare(record.notification));
    {
      StateGuard guard(gate);
      if (!error) {
        input.notify = true;
        auto admitted = engine.admit(record.request, input);
        if (!admitted)
          error = admitted.error();
      }
      record.phase = error ? Phase::Rollback : Phase::Accepted;
      if (error && retain_failure) {
        auto rejected = engine.admit_rejected(record.request, input, error);
        if (rejected)
          record.phase = Phase::Rejected;
        else
          error = rejected.error();
      }
      if (observation && record.phase != Phase::Rollback) {
        // This is the fresh request's first external reference. No caller can
        // consume its result between publication and this retain.
        if (!record.request.state().retain(observation->reference))
          __builtin_trap();
        observation->owner = this;
        observation->request = &record.request;
      }
    }
    return error ? ErrorOr<void>(Error(error)) : ErrorOr<void>();
  }

  LIBC_INLINE int retain(typename Engine::Request &request,
                         CancelObservation &observation) {
    if (observation.used)
      return EINVAL;
    auto retained = request.state().retain(observation.reference);
    if (!retained)
      return retained.error();
    observation.owner = this;
    observation.request = &request;
    observation.used = true;
    observation.completed_at_capture = request.complete();
    return 0;
  }

  LIBC_INLINE void dispose_batch(CancelBatch &batch) {
    for (size_t i = 0; i < batch.capacity; ++i)
      batch.items()[i].~CancelObservation();
    batch.~CancelBatch();
    allocator.deallocate(&batch);
  }

  // Sole maintainer, after the caller has relinquished all batch access.
  LIBC_INLINE int maintain_batch(CancelBatch &batch) {
    {
      StateGuard guard(gate);
      if (!batch.released)
        return 0;
    }
    int error = 0;
    for (size_t i = 0; i < batch.count; ++i) {
      auto &item = batch.items()[i];
      if (!item.owner)
        continue;
      int result = release_cancel(item);
      if (!error)
        error = result;
    }
    if (error)
      return error;
    {
      StateGuard guard(gate);
      CancelBatch **link = &cancel_batches;
      while (*link != &batch)
        link = &(*link)->next;
      *link = batch.next;
    }
    dispose_batch(batch);
    return 0;
  }

  // Called with the sole maintainer claim, not a held mutex. Preparing records
  // are pinned by their submitting caller; only this maintainer may destroy
  // accepted/rollback records. Notification calls may reenter admission.
  LIBC_INLINE int maintain_record(Record &record) {
    bool rollback, rejected;
    {
      StateGuard guard(gate);
      if (record.phase == Phase::Preparing)
        return 0;
      rollback = record.phase == Phase::Rollback;
      rejected = record.phase == Phase::Rejected;
      if (!rollback && !rejected && !record.notification_claimed) {
        if (!record.request.complete())
          return 0;
        if (!record.request.state().take_notification())
          __builtin_trap();
        record.notification_claimed = true;
      }
    }
    int error = 0;
    if (!record.notification_terminal) {
      if (rollback || rejected) {
        record.notification.discard();
      } else {
        error = int(record.notification.activate());
        if (error == EAGAIN || error == EBUSY)
          return error;
      }
      record.notification_terminal = true;
    }
    int cleanup_error = int(record.notification.cleanup());
    if (!error)
      error = cleanup_error;
    bool drained = record.notification.drained();
    if (rollback) {
      // Independent rollback owners: a callback cleanup failure must not lose
      // the personality, and failed unregister must not lose the callback.
      bool resources_drained;
      {
        RingGuard lock(ring_operations.value);
        auto released = record.request.resources.release();
        if (!error && !released)
          error = released.error();
        resources_drained = record.request.resources.drained();
      }
      if (drained && resources_drained)
        destroy(record);
      return error;
    }
    if (!drained)
      return error;
    bool retired;
    {
      RingGuard lock(ring_operations.value);
      {
        StateGuard guard(gate);
        if (!rejected && !record.notification_released) {
          if (!record.request.state().release_notification())
            __builtin_trap();
          record.notification_released = true;
        }
        if (!engine.begin_retire(record.request))
          return error;
      }
      auto released = record.request.resources.release();
      if (!error && !released)
        error = released.error();
      {
        StateGuard guard(gate);
        retired = engine.finish_retire(record.request);
      }
    }
    if (retired)
      destroy(record);
    return error;
  }

public:
  AioServiceRegistry(RingLike &ring, Allocator &allocator, ResourceOps ops,
                     uint64_t &watermark, uint64_t process,
                     typename Engine::Completion completion = nullptr,
                     void *completion_context = nullptr)
      : ring(ring), allocator(allocator), resource_ops(ops),
        completion(completion), completion_context(completion_context),
        engine(ring, watermark, process, UINT32_MAX, publish_completion, this) {
  }
  AioServiceRegistry(const AioServiceRegistry &) = delete;
  AioServiceRegistry &operator=(const AioServiceRegistry &) = delete;
  ~AioServiceRegistry() {
    if (records || cancel_batches || storage.size() || maintaining)
      __builtin_trap();
  }

  // Exclusive bootstrap only, on the already initialized ring. This is not
  // lazy initialization; capacity is the existing transport credit budget.
  LIBC_INLINE ErrorOr<void> initialize(uint32_t capacity) {
    return engine.initialize(capacity);
  }

  // Input is an already validated/captured public-operation snapshot; no aiocb
  // fields are ever dereferenced. prepare(Notification &) returns positive
  // errno or zero, e.g. notification.prepare(event, image, page_size), and runs
  // on the submitting thread outside both locks. It must obey Notification's
  // preparation contract, including retaining partial failure resources.
  // Every record takes the engine notification claim, even for SIGEV_NONE:
  // prepare may own callback rollback/reaping resources independently of the
  // requested delivery mode. Only drained() releases that claim. Input.notify
  // is ignored here; the prepared Notification selects delivery, not this bit.
  // Failure retains rollback in this service; maintain() must keep running.
  template <class Prepare>
  LIBC_INLINE ErrorOr<void> admit(Input input, Prepare prepare) {
    auto *slot = acquire();
    if (!slot)
      return Error(ENOMEM);
    auto *record = new (slot->storage()) Record(ring, resource_ops, slot);
    {
      StateGuard guard(gate);
      record->next = records;
      records = record;
    }
    return admit_record(*record, input, prepare, false);
  }

  // No fd capture, notification creation, request acceptance or SQ publication.
  // A prepared record pins storage but is invisible to public result queries.
  // The list owner excludes duplicate/active aiocbs before starting members;
  // concurrent reuse of one aiocb is not made valid by this API.
  LIBC_INLINE ErrorOr<void> prepare_member(PreparedMember &member,
                                           Input input) {
    if (member.owner || !input.control_block)
      return Error(EINVAL);
    auto *slot = acquire();
    if (!slot)
      return Error(ENOMEM);
    auto *record = new (slot->storage()) Record(ring, resource_ops, slot);
    StateGuard guard(gate);
    record->next = records;
    records = record;
    auto reserved = engine.prepare_rejection(record->request);
    if (!reserved || engine.find(input.control_block)) {
      record->phase = Phase::Rollback;
      return Error(reserved ? EBUSY : reserved.error());
    }
    member.owner = this;
    member.record = record;
    member.input = input;
    return {};
  }

  LIBC_INLINE int abandon_member(PreparedMember &member) {
    if (member.owner != this)
      return EINVAL;
    StateGuard guard(gate);
    static_cast<Record *>(member.record)->phase = Phase::Rollback;
    member.owner = nullptr;
    member.record = nullptr;
    return 0;
  }

  // Consumes the preparation on success OR failure. Ordinary admission errors
  // become queryable local results, with no further allocation. Failure caused
  // by conflicting aiocb reuse preserves the existing binding instead. Failed
  // callback/resource preparation is retained for worker cleanup as usual.
  template <class Prepare>
  LIBC_INLINE ErrorOr<void>
  start_member(PreparedMember &member, Prepare prepare,
               MemberObservation *observation = nullptr) {
    if (member.owner != this || (observation && observation->owner))
      return Error(EINVAL);
    auto *record = static_cast<Record *>(member.record);
    Input input = member.input;
    member.owner = nullptr;
    member.record = nullptr;
    return admit_record(*record, input, prepare, true, observation);
  }

  LIBC_INLINE ErrorOr<AioRequest::Result>
  member_result(const MemberObservation &observation) {
    typename Gate::Guard guard(gate);
    if (!guard)
      return Error(guard.error());
    if (observation.owner != this)
      return Error(EINVAL);
    return observation.request->state().result(observation.reference);
  }

  LIBC_INLINE int release_member(MemberObservation &observation) {
    StateGuard guard(gate);
    if (observation.owner != this)
      return EINVAL;
    if (!observation.request->state().release(observation.reference))
      __builtin_trap();
    observation.owner = nullptr;
    observation.request = nullptr;
    return 0;
  }

  LIBC_INLINE ErrorOr<int> error(const void *control_block) {
    typename Gate::Guard guard(gate);
    if (!guard)
      return Error(guard.error());
    return engine.error(control_block);
  }

  // Publish a never-accepted list member's failure without capturing its fd or
  // preparing a notification. Storage failure is still synchronous: a list
  // transaction must reserve all result storage before starting any member.
  // This primitive alone does not implement that list-wide transaction.
  LIBC_INLINE ErrorOr<void> admit_rejected(Input input, int failure) {
    if (!input.control_block || failure <= 0 || failure == EINPROGRESS)
      return Error(EINVAL);
    auto *slot = acquire();
    if (!slot)
      return Error(ENOMEM);
    auto *record = new (slot->storage()) Record(ring, resource_ops, slot);
    StateGuard guard(gate);
    record->next = records;
    records = record;
    auto result = engine.admit_rejected(record->request, input, failure);
    record->phase = result ? Phase::Rejected : Phase::Rollback;
    return result;
  }
  LIBC_INLINE ErrorOr<AioRequest::Result>
  take_result(const void *control_block) {
    typename Gate::Guard guard(gate);
    if (!guard)
      return Error(guard.error());
    return engine.take_result(control_block);
  }
  LIBC_INLINE ErrorOr<bool> complete(const void *control_block) {
    typename Gate::Guard guard(gate);
    if (!guard)
      return Error(guard.error());
    auto *request = engine.find(control_block);
    return request ? ErrorOr<bool>(request->complete())
                   : ErrorOr<bool>(Error(EINVAL));
  }

  LIBC_INLINE int retain_cancel(const void *key,
                                CancelObservation &observation) {
    typename Gate::Guard guard(gate);
    if (!guard)
      return guard.error();
    auto *request = engine.find(key);
    if (!request)
      return EINVAL;
    return retain(*request, observation);
  }

  // Sizing is advisory: allocation happens outside the gate, so snapshot
  // admission must recheck capacity. The service owns that stable allocation
  // until EVERY original/control observation has been explicitly released.
  LIBC_INLINE ErrorOr<size_t> cancel_count(int descriptor) {
    typename Gate::Guard guard(gate);
    if (!guard)
      return Error(guard.error());
    size_t count = 0;
    engine.visit_bound(descriptor, [&](auto &) { ++count; });
    return count;
  }

  // All-or-none reference acquisition at one gate epoch, with no syscalls or
  // allocation. ERANGE means capacity changed/was insufficient, not a partial
  // snapshot. Later bindings are excluded even if their address/fd is reused.
  // Completed but unconsumed results are included; unbound results are not.
  LIBC_INLINE ErrorOr<size_t>
  retain_cancels(int descriptor, CancelObservation *output, size_t capacity) {
    typename Gate::Guard guard(gate);
    if (!guard)
      return Error(guard.error());
    size_t count = 0;
    engine.visit_bound(descriptor, [&](auto &) { ++count; });
    if (count > capacity)
      return Error(ERANGE);
    if (count && !output)
      return Error(EINVAL);
    for (size_t i = 0; i < count; ++i)
      if (output[i].used)
        return Error(EINVAL);
    size_t acquired = 0;
    int error = 0;
    engine.visit_bound(descriptor, [&](auto &request) {
      if (!error) {
        error = retain(request, output[acquired]);
        if (!error)
          ++acquired;
      }
    });
    if (error) {
      for (size_t i = 0; i < acquired; ++i) {
        auto &observation = output[i];
        if (!observation.request->state().release(observation.reference))
          __builtin_trap();
        observation.owner = nullptr;
        observation.request = nullptr;
        observation.used = false;
      }
      return Error(error);
    }
    return acquired;
  }

  // Zero means the attempt was recorded, NOT that the original I/O has been
  // cancelled. On every return the independent observation remains owned.
  // Only original_cancel_result reports the original request's termination.
  LIBC_INLINE int request_cancel(CancelObservation &observation) {
    typename Gate::Guard guard(gate);
    if (!guard)
      return guard.error();
    if (observation.owner != this || observation.attempted)
      return EINVAL;
    observation.attempted = true;
    auto &request = *observation.request;
    if (request.complete() || engine.cancel_queued(request))
      return 0;
    auto prepared = engine.cancel_published(request, observation.control);
    observation.controlled = bool(prepared);
    return prepared ? 0 : prepared.error();
  }

  LIBC_INLINE ErrorOr<AioRequest::Result>
  original_cancel_result(const CancelObservation &observation) {
    typename Gate::Guard guard(gate);
    if (!guard)
      return Error(guard.error());
    if (observation.owner != this)
      return Error(EINVAL);
    return observation.request->state().result(observation.reference);
  }

  LIBC_INLINE ErrorOr<int>
  cancel_control_result(const CancelObservation &observation) {
    typename Gate::Guard guard(gate);
    if (!guard)
      return Error(guard.error());
    if (observation.owner != this || !observation.controlled)
      return Error(EINVAL);
    return engine.cancel_result(observation.control);
  }

  // Ordinary service transaction only. A null key snapshots this descriptor's
  // bindings; otherwise it captures exactly the supplied binding. The public
  // wrapper must validate the descriptor itself (an empty snapshot is not an
  // EBADF check). Growth between sizing and capture returns ERANGE with no
  // partial snapshot or cancel attempt; do not spin under the state gate.
  LIBC_INLINE ErrorOr<CancelBatch *>
  prepare_cancels(int descriptor, const void *key = nullptr) {
    auto size = key ? ErrorOr<size_t>(size_t(1)) : cancel_count(descriptor);
    if (!size)
      return Error(size.error());
    if (*size > (SIZE_MAX - sizeof(CancelBatch)) / sizeof(CancelObservation))
      return Error(ENOMEM);
    void *memory = allocator.allocate(sizeof(CancelBatch) +
                                          *size * sizeof(CancelObservation),
                                      alignof(CancelBatch));
    if (!memory)
      return Error(ENOMEM);
    auto *batch = new (memory) CancelBatch(this, *size);
    for (size_t i = 0; i < *size; ++i)
      new (&batch->items()[i]) CancelObservation;
    ErrorOr<size_t> captured = size_t(0);
    if (key) {
      int error = retain_cancel(key, batch->items()[0]);
      captured =
          error ? ErrorOr<size_t>(Error(error)) : ErrorOr<size_t>(size_t(1));
    } else {
      captured = retain_cancels(descriptor, batch->items(), *size);
    }
    if (!captured) {
      dispose_batch(*batch);
      return Error(captured.error());
    }
    batch->count = *captured;
    {
      StateGuard guard(gate);
      batch->next = cancel_batches;
      cancel_batches = batch;
    }
    return batch;
  }

  // All members are attempted even if a control reservation fails. The first
  // positive errno is diagnostic, not evidence of original completion. The
  // caller must finish_cancels on EVERY return path, including failure.
  LIBC_INLINE int attempt_cancels(CancelBatch &batch) {
    {
      StateGuard guard(gate);
      if (batch.owner != this || batch.released || batch.attempted)
        return EINVAL;
      batch.attempted = true;
    }
    int error = 0;
    for (size_t i = 0; i < batch.count; ++i) {
      int result = request_cancel(batch.items()[i]);
      if (!error)
        error = result;
    }
    return error;
  }

  // One state epoch. A successful cancel-control CQE never means Canceled;
  // only an original ECANCELED result newly observed by this snapshot does.
  // Pending originals dominate canceled/completed members. An empty batch,
  // or one consisting solely of already completed requests, is AllDone.
  LIBC_INLINE ErrorOr<CancelStatus> cancel_status(CancelBatch &batch) {
    typename Gate::Guard guard(gate);
    if (!guard)
      return Error(guard.error());
    if (batch.owner != this || batch.released || !batch.attempted)
      return Error(EINVAL);
    bool canceled = false;
    for (size_t i = 0; i < batch.count; ++i) {
      auto &item = batch.items()[i];
      auto result = item.request->state().result(item.reference);
      if (!result) {
        if (result.error() == EINPROGRESS)
          return CancelStatus::NotCanceled;
        return Error(result.error());
      }
      canceled |= !item.completed_at_capture && result->error == ECANCELED;
    }
    return canceled ? CancelStatus::Canceled : CancelStatus::AllDone;
  }

  // No use of batch is permitted after success; the worker may free it before
  // this function returns. Does not consume the application's I/O result.
  LIBC_INLINE int finish_cancels(CancelBatch &batch) {
    typename Gate::Guard guard(gate);
    if (!guard)
      return guard.error();
    if (batch.owner != this || batch.released)
      return EINVAL;
    batch.released = true;
    return 0;
  }

  LIBC_INLINE bool has_cancel_batches() {
    StateGuard guard(gate);
    return cancel_batches != nullptr;
  }

  // EBUSY retains BOTH owners until the worker consumes/withdraws the control.
  // This can succeed before original completion: the original operation's own
  // kernel/public-result ownership remains untouched. No physical cleanup here.
  LIBC_INLINE int release_cancel(CancelObservation &observation) {
    typename Gate::Guard guard(gate);
    if (!guard)
      return guard.error();
    if (observation.owner != this)
      return EINVAL;
    if (observation.controlled && !engine.retire_cancel(observation.control))
      return EBUSY;
    if (!observation.request->state().release(observation.reference))
      __builtin_trap();
    observation.owner = nullptr;
    observation.request = nullptr;
    return 0;
  }

  // Allocation-free query/wait entry. The caller pins this service through
  // finish or jump cleanup and supplies the owning thread's actual cleanup
  // head. No ordinary admission/ring lock is taken while sleeping. Public
  // cancellation and relative-time conversion remain wrapper responsibilities.
  LIBC_INLINE Wait make_wait(JumpCleanupStack::Node *&head) {
    return Wait(observers, gate, head);
  }
  // Preserve the actual pointer element type: aiocb** is not a void** array.
  LIBC_INLINE int begin_wait(Wait &wait, const ControlBlock *const *list,
                             size_t count) {
    if (!wait.belongs_to(observers, gate))
      return EINVAL;
    return wait.begin(list, count, [this](const void *key) -> ErrorOr<bool> {
      // The registration already owns our state gate, so do not call the
      // public query helper and recursively acquire it.
      auto *request = engine.find(key);
      return request ? ErrorOr<bool>(request->complete())
                     : ErrorOr<bool>(Error(EINVAL));
    });
  }
  LIBC_INLINE size_t retained() {
    StateGuard guard(gate);
    return storage.size();
  }

  // Exclusive copied child, with the dispatcher parked before clone. Never
  // acquire copied locks, retire callbacks, unregister parent personalities or
  // consume CQEs. The caller must quarantine this entire instance permanently.
  LIBC_INLINE int quarantine_child() {
    int error = 0;
    for (Record *record = records; record; record = record->next) {
      auto result = record->request.resources.quarantine_child();
      if (!result && !error)
        error = result.error();
    }
    return error;
  }

  // Worker only. Queries remain available inside ring.submit(), including
  // after terminal CQ publication. Admission never calls this path.
  LIBC_INLINE int submit() {
    RingGuard lock(ring_operations.value);
    int error = begin_submit();
    return error ? error : finish_submit(ring.submit());
  }

  // One pass, no waiting for callbacks or retry-until-success loops. A second
  // (including reentrant) maintainer returns EBUSY without waiting. Returns the
  // first diagnostic; neither success nor failure authorizes destruction of a
  // nonempty service. Call again for retained notification/release errors.
  LIBC_INLINE int maintain() {
    Record *record;
    CancelBatch *batch;
    {
      StateGuard guard(gate);
      if (maintaining)
        return EBUSY;
      maintaining = true;
      record = records;
      batch = cancel_batches;
    }
    int error = 0;
    while (batch) {
      CancelBatch *next;
      {
        StateGuard guard(gate);
        next = batch->next;
      }
      int result = maintain_batch(*batch);
      if (!error)
        error = result;
      batch = next;
    }
    while (record) {
      Record *next;
      {
        StateGuard guard(gate);
        // Admissions only prepend; no other maintainer can unlink this chain.
        // New heads can wait for the next pass. Preparing records are skipped
        // unless admission has published their final phase before inspection.
        next = record->next;
      }
      int result = maintain_record(*record);
      if (!error)
        error = result;
      record = next;
    }
    for (;;) {
      typename Storage::Block *block;
      {
        StateGuard guard(gate);
        block = storage.detach_empty();
      }
      if (!block)
        break;
      if (!Storage::dispose(block))
        __builtin_trap();
    }
    {
      StateGuard guard(gate);
      maintaining = false;
    }
    return error;
  }
};

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_AIO_SERVICE_REGISTRY_H
