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
// REQUIRED EXTERNAL GUARDS: every admit/submit/dispatch_notifications/maintain
// call holds a service lifetime pin and excludes fork/exec/exit transitions.
// It suppresses application signals AND cancellation for its ENTIRE duration,
// including allocation, notification preparation, lock waits and rollback.
// AioGate only protects short state regions, not these outer guards. Queries
// need the stable service lifetime but only use AioGate, never the ring lock.
// Neither preparation nor maintenance is an application-handler entrypoint.
// The credential handler must not enter this service. Destruction requires
// exclusive lifetime ownership, no calls in flight and fully drained records.
//
// Queue and resource operations have independent locks, each before gate,
// never the reverse. Do not nest the two operation locks. Allocation,
// notification methods and resource syscalls run OUTSIDE gate. Notification
// and allocator methods also run without either operation lock: their
// loader/runtime locks may be held by a concurrent admission caller. Closing
// a terminal request's resources must not prevent other requests from being
// submitted or completed. Personality registration/release does not access
// user queue indices and the kernel serializes its own personality table.
// Notification methods never synchronously invoke application code; a launched
// callback may use the service on its own thread. Preparation must not wait for
// accepted I/O. Allocator follows AioStorage's concurrent detached-block
// contract.
// Failed admission synchronously calls withdraw_failed_admission() without
// any gate or operation lock. It accepts Fresh, failed prepare and Prepared
// notifications, stops caller-stack threads without callbacks, and leaves
// independent cleanup resources owned by the notification for maintain().
//
// SlotsPerBlock is allocation granularity, not an admission limit. Kernel
// credits bound in-flight work, independently of retained results/rollback.
// UINT32_MAX below is the dependency counter's representational bound, not CQ
// capacity or a new resource policy. No public capability or limit is selected.
// Notification/thread admission policy remains with the external runtime.
//
// Deferred owners: public cancellation/list wrappers,
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

  // The public wrapper removes NULL/NOP entries and snapshots the remaining
  // inputs. A malformed member has its own positive error, not an aggregate
  // validation error. Flags, list limits and duplicate aiocbs are checked
  // before any member is started. This layer does not borrow the public list.
  struct ListInput {
    Input input;
    int error = 0;
  };

private:
  struct ListMember {
    PreparedMember prepared;
    MemberObservation observation;
    int error = 0;
  };

public:
  // Stable service allocation: one caller owns preparation/status access until
  // finish_list. That release does not cancel members or suppress notification.
  // The maintainer retains the group until all members and notification owners
  // have finished. WAIT supplies an inert notification, never the public sig.
  class alignas(ListMember) ListGroup {
    friend class AioServiceRegistry;
    enum class Phase { Preparing, Active, Rollback };
    AioServiceRegistry *owner;
    ListGroup *next = nullptr;
    size_t count;
    Phase phase = Phase::Preparing;
    Notification notification;
    AioWaitEvent completion;
    int error = 0;
    bool complete = false;
    bool released = false;
    // Gate-protected handoff: busy pins dispatch; terminal permits cleanup.
    bool notification_busy = false;
    bool notification_terminal = false;

    ListGroup(AioServiceRegistry *owner, size_t count)
        : owner(owner), count(count) {}
    ListMember *items() { return reinterpret_cast<ListMember *>(this + 1); }

  public:
    ListGroup(const ListGroup &) = delete;
    ListGroup &operator=(const ListGroup &) = delete;
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
  struct NoNormalization {
    int operator()(int, Input &) const { return 0; }
    int operator()(size_t, int, Input &) const { return 0; }
  };

  enum class Phase { Preparing, Accepted, Rejected, Rollback };
  struct Record {
    typename Engine::Request request;
    Notification notification;
    Record *next = nullptr;
    // AioStorage needs the complete Record type before defining its Slot.
    void *lease;
    Phase phase = Phase::Preparing;
    bool notification_claimed = false;
    // Gate-protected handoff: busy pins dispatch; terminal permits cleanup.
    bool notification_busy = false;
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
  struct alignas(8) {
    RawMutex value;
  } resource_operations;
  AioSuspendObservers<ControlBlock> observers;
  typename Engine::Completion completion;
  void *completion_context;
  Engine engine;
  Storage storage;
  Record *records = nullptr;
  CancelBatch *cancel_batches = nullptr;
  ListGroup *list_groups = nullptr;
  bool maintaining = false;
  bool dispatching = false;
  // Gate-protected next candidates, repaired by the sole maintainer on unlink.
  // Only a busy notification owner may escape the gate in the dispatch pass.
  Record *notification_record = nullptr;
  ListGroup *notification_group = nullptr;

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
      if (notification_record == &record)
        notification_record = record.next;
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
    int error = engine.stage();
    update_lists();
    return error;
  }
  LIBC_INLINE int finish_submit(ErrorOr<unsigned> result) {
    StateGuard guard(gate);
    int error = engine.finish_submit(result);
    update_lists();
    return error;
  }

  template <class Prepare, class Normalize>
  LIBC_INLINE ErrorOr<void>
  admit_record(Record &record, Input input, Prepare prepare,
               Normalize normalize, bool retain_failure,
               MemberObservation *observation = nullptr) {
    int error = 0;
    {
      RingGuard lock(resource_operations.value);
      auto captured = record.request.resources.capture(input.descriptor);
      if (!captured)
        error = captured.error();
    }
    if (!error) {
      Input normalized = input;
      error = int(normalize(record.request.resources.descriptor(), normalized));
      if (normalized.control_block != input.control_block ||
          normalized.descriptor != input.descriptor ||
          normalized.operation != input.operation ||
          normalized.notify != input.notify || error < 0 ||
          error == EINPROGRESS || !Engine::valid_transfer_flags(normalized))
        error = EINVAL;
      // Preserve the original binding and policy even for retained failures.
      if (!error) {
        input.buffer = normalized.buffer;
        input.size = normalized.size;
        input.offset = normalized.offset;
        input.transfer_flags = normalized.transfer_flags;
      }
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
      if (!error) {
        record.phase = Phase::Accepted;
        if (observation) {
          if (!record.request.state().retain(observation->reference))
            __builtin_trap();
          observation->owner = this;
          observation->request = &record.request;
        }
        return {};
      }
    }
    // Preparing pins the owner across withdrawal, including reentrant workers.
    // Do not publish even a rejected result until caller-stack use has stopped.
    record.notification.withdraw_failed_admission();
    {
      StateGuard guard(gate);
      record.phase = Phase::Rollback;
      record.notification_terminal = true;
      if (retain_failure) {
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
    return Error(error);
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

  LIBC_INLINE void dispose_list(ListGroup &group) {
    for (size_t i = 0; i < group.count; ++i)
      group.items()[i].~ListMember();
    group.~ListGroup();
    allocator.deallocate(&group);
  }

  // Gate held. Observe the original requests, never look up a reused aiocb.
  LIBC_INLINE void update_list(ListGroup &group) {
    if (group.complete)
      return;
    bool complete = true;
    for (size_t i = 0; i < group.count; ++i) {
      auto &observation = group.items()[i].observation;
      if (!observation.owner)
        continue;
      auto result = observation.request->state().result(observation.reference);
      if (!result) {
        if (result.error() != EINPROGRESS)
          __builtin_trap();
        complete = false;
      } else if (result->error && !group.error)
        group.error = EIO;
    }
    group.complete = complete;
    if (complete)
      group.completion.publish();
  }

  // Gate held. Wake list waiters in the completion path even if the notifier
  // or maintainer is blocked in another record's notification work.
  LIBC_INLINE void update_lists() {
    for (ListGroup *group = list_groups; group; group = group->next)
      if (group->phase == ListGroup::Phase::Active)
        update_list(*group);
  }

  LIBC_INLINE int maintain_list(ListGroup &group) {
    {
      StateGuard guard(gate);
      if (group.phase == ListGroup::Phase::Preparing)
        return 0;
      if (group.phase != ListGroup::Phase::Rollback) {
        if (!group.complete)
          return 0;
        // Result observations can drain even while group delivery retries or
        // is in flight; notification methods never access these references.
        for (size_t i = 0; i < group.count; ++i) {
          auto &observation = group.items()[i].observation;
          if (!observation.owner)
            continue;
          if (!observation.request->state().release(observation.reference))
            __builtin_trap();
          observation.owner = nullptr;
          observation.request = nullptr;
        }
      }
      if (group.notification_busy || !group.notification_terminal)
        return 0;
    }
    int error = int(group.notification.cleanup());
    if (!group.notification.drained())
      return error;
    {
      StateGuard guard(gate);
      if (!group.released)
        return error;
      ListGroup **link = &list_groups;
      while (*link != &group)
        link = &(*link)->next;
      if (notification_group == &group)
        notification_group = group.next;
      *link = group.next;
    }
    dispose_list(group);
    return error;
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
  // accepted/rollback records. A terminal notification has been handed off by
  // admission or the notifier and will never be accessed by that owner again.
  LIBC_INLINE int maintain_record(Record &record) {
    bool rollback, rejected;
    {
      StateGuard guard(gate);
      if (record.phase == Phase::Preparing || record.notification_busy ||
          !record.notification_terminal)
        return 0;
      rollback = record.phase == Phase::Rollback;
      rejected = record.phase == Phase::Rejected;
    }
    int error = int(record.notification.cleanup());
    bool drained = record.notification.drained();
    if (rollback) {
      // Independent rollback owners: a callback cleanup failure must not lose
      // the personality, and failed unregister must not lose the callback.
      bool resources_drained;
      {
        RingGuard lock(resource_operations.value);
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
      RingGuard lock(resource_operations.value);
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
    if (records || cancel_batches || list_groups || storage.size() ||
        maintaining || dispatching)
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
  // Failure retains rollback; dispatch_notifications() and maintain() must
  // keep running.
  // Optional normalize(int retained_fd, Input &) runs after successful capture,
  // before prepare/admission, outside BOTH locks on the submitting thread.
  // It returns zero or positive errno (not EINPROGRESS); invalid status or
  // mutation of control_block/descriptor/operation/notify yields EINVAL. Only
  // checked buffer/size/offset and append-policy changes are committed. The fd
  // is the borrowed private duplicate, not Input.descriptor (the scheduling
  // key). Do not close/replace it, change shared OFD flags/position, perform
  // blocking I/O or wait for accepted work. Non-positioned/nonblocking policy
  // stays with the caller/engine; this hook does not authorize changing
  // descriptor flags. Queries and ordinary service reentry are permitted under
  // the outer guards. Neither closure nor Input reference may escape this
  // synchronous call.
  template <class Prepare, class Normalize = NoNormalization>
  LIBC_INLINE ErrorOr<void> admit(Input input, Prepare prepare,
                                  Normalize normalize = {}) {
    auto *slot = acquire();
    if (!slot)
      return Error(ENOMEM);
    auto *record = new (slot->storage()) Record(ring, resource_ops, slot);
    {
      StateGuard guard(gate);
      record->next = records;
      records = record;
    }
    return admit_record(*record, input, prepare, normalize, false);
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
  // notification is synchronously withdrawn; independent resources remain
  // owned for worker cleanup.
  // normalize has the same contract as admit, including on retained failure.
  template <class Prepare, class Normalize = NoNormalization>
  LIBC_INLINE ErrorOr<void>
  start_member(PreparedMember &member, Prepare prepare,
               MemberObservation *observation = nullptr,
               Normalize normalize = {}) {
    if (member.owner != this || (observation && observation->owner))
      return Error(EINVAL);
    auto *record = static_cast<Record *>(member.record);
    Input input = member.input;
    member.owner = nullptr;
    member.record = nullptr;
    return admit_record(*record, input, prepare, normalize, true, observation);
  }

  // Consume a reserved malformed member without touching its fd or event.
  LIBC_INLINE ErrorOr<void> reject_member(PreparedMember &member, int error,
                                          MemberObservation &observation) {
    if (member.owner != this || observation.owner || error <= 0 ||
        error == EINPROGRESS)
      return Error(EINVAL);
    auto *record = static_cast<Record *>(member.record);
    Input input = member.input;
    member.owner = nullptr;
    member.record = nullptr;
    StateGuard guard(gate);
    auto result = engine.admit_rejected(record->request, input, error);
    record->phase = result ? Phase::Rejected : Phase::Rollback;
    if (result) {
      if (!record->request.state().retain(observation.reference))
        __builtin_trap();
      observation.owner = this;
      observation.request = &record->request;
    }
    return result;
  }

  // No I/O starts until every member and the group notification are prepared.
  // A failed preparation remains privately owned for deferred cleanup; it does
  // not create public results. Inputs and the supplied preparation closure are
  // used only during this call. No fixed retained-list limit is imposed here.
  template <class Prepare>
  LIBC_INLINE ErrorOr<ListGroup *> prepare_list(const ListInput *inputs,
                                                size_t count, Prepare prepare) {
    if (count && !inputs)
      return Error(EINVAL);
    if (count > (SIZE_MAX - sizeof(ListGroup)) / sizeof(ListMember))
      return Error(ENOMEM);
    for (size_t i = 0; i < count; ++i) {
      if (!inputs[i].input.control_block || inputs[i].error < 0 ||
          inputs[i].error == EINPROGRESS)
        return Error(EINVAL);
      for (size_t j = 0; j < i; ++j)
        if (inputs[i].input.control_block == inputs[j].input.control_block)
          return Error(EINVAL);
    }
    void *memory = allocator.allocate(
        sizeof(ListGroup) + count * sizeof(ListMember), alignof(ListGroup));
    if (!memory)
      return Error(ENOMEM);
    auto *group = new (memory) ListGroup(this, count);
    for (size_t i = 0; i < count; ++i)
      new (&group->items()[i]) ListMember;
    {
      StateGuard guard(gate);
      group->next = list_groups;
      list_groups = group;
    }
    size_t prepared = 0;
    int error = 0;
    for (; prepared < count; ++prepared) {
      auto &item = group->items()[prepared];
      item.error = inputs[prepared].error;
      auto result = prepare_member(item.prepared, inputs[prepared].input);
      if (!result) {
        error = result.error();
        break;
      }
    }
    if (!error)
      error = int(prepare(group->notification));
    if (error) {
      group->notification.withdraw_failed_admission();
      for (size_t i = 0; i < prepared; ++i)
        if (abandon_member(group->items()[i].prepared))
          __builtin_trap();
      StateGuard guard(gate);
      group->released = true;
      group->notification_terminal = true;
      group->phase = ListGroup::Phase::Rollback;
      return Error(error);
    }
    return group;
  }

  // Starts all prepared members, including local failures. Queue resource
  // failures report aggregate EAGAIN; other member failures report EIO. Neither
  // rolls back successful members. The caller
  // must finish_list on every return after start_list. Its prepare closure may
  // snapshot per-member events by index; malformed members never invoke it.
  // normalize(size_t index, int retained_fd, Input &) follows admit's contract;
  // malformed members skip it, and normalization failure skips member prepare.
  // The independent group notification was already prepared by prepare_list.
  template <class Prepare, class Normalize = NoNormalization>
  LIBC_INLINE int start_list(ListGroup &group, Prepare prepare,
                             Normalize normalize = {}) {
    if (group.owner != this || group.phase != ListGroup::Phase::Preparing)
      return EINVAL;
    int error = 0;
    bool accepted = false;
    for (size_t i = 0; i < group.count; ++i) {
      auto &item = group.items()[i];
      auto result = item.error ? reject_member(item.prepared, item.error,
                                               item.observation)
                               : start_member(
                                     item.prepared,
                                     [&](Notification &notification) {
                                       return prepare(i, notification);
                                     },
                                     &item.observation,
                                     [&](int descriptor, Input &input) {
                                       return normalize(i, descriptor, input);
                                     });
      int failure = item.error ? item.error : (result ? 0 : result.error());
      accepted |= !failure;
      if (failure == EAGAIN || failure == ENOMEM || failure == EMFILE ||
          failure == ENFILE)
        error = EAGAIN;
      else if (failure && !error)
        error = EIO;
    }
    const bool withdraw = group.count && !accepted;
    if (withdraw)
      group.notification.withdraw_failed_admission();
    StateGuard guard(gate);
    group.error = error;
    group.notification_terminal = withdraw;
    // Even all-rejected groups own observations, released by the Active path.
    group.phase = ListGroup::Phase::Active;
    update_list(group);
    return error;
  }

  // Result is independent of member aio_return and group notification delivery.
  // EINPROGRESS is not permission to release request buffers. Even EIO is only
  // returned once every started member has terminated. A queue-resource EAGAIN
  // remains distinct from a completed member's I/O error.
  LIBC_INLINE ErrorOr<int> list_result(ListGroup &group) {
    typename Gate::Guard guard(gate);
    if (!guard)
      return Error(guard.error());
    if (group.owner != this || group.released ||
        group.phase != ListGroup::Phase::Active)
      return Error(EINVAL);
    update_list(group);
    return group.complete ? ErrorOr<int>(group.error)
                          : ErrorOr<int>(Error(EINPROGRESS));
  }

  LIBC_INLINE int finish_list(ListGroup &group) {
    typename Gate::Guard guard(gate);
    if (!guard)
      return guard.error();
    if (group.owner != this || group.released ||
        group.phase != ListGroup::Phase::Active)
      return EINVAL;
    group.released = true;
    return 0;
  }

  // One owning waiter, no state/ring lock held across the syscall. Timeout or
  // interruption keeps the group owned; the wrapper must finish_list on every
  // departure (including cancellation). The group, not a caller stack frame,
  // owns the published event. A zero return means all original results exist,
  // not that every member or notification succeeded; consult list_result.
  LIBC_INLINE int wait_list(ListGroup &group,
                            const timespec *deadline = nullptr,
                            AioWaitEvent::WaitSyscall wait_syscall = nullptr,
                            void *context = nullptr) {
    {
      typename Gate::Guard guard(gate);
      if (!guard)
        return guard.error();
      if (group.owner != this || group.released ||
          group.phase != ListGroup::Phase::Active)
        return EINVAL;
      update_list(group);
    }
    return wait_syscall ? group.completion.wait(deadline, wait_syscall, context)
                        : group.completion.wait(deadline);
  }

  LIBC_INLINE bool has_list_groups() {
    StateGuard guard(gate);
    return list_groups != nullptr;
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
    if (request.complete() || engine.cancel_queued(request)) {
      update_lists();
      return 0;
    }
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

  // All preparation, completion, notification and maintenance actors must be
  // parked. Queries use only this short gate; a live stack waiter prevents
  // the snapshot even if its request has already completed and retired.
  LIBC_INLINE bool try_freeze() {
    if (!gate.try_freeze())
      return false;
    if (observers.empty())
      return true;
    gate.thaw();
    return false;
  }
  LIBC_INLINE void thaw() { gate.thaw(); }

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

  // One notification attempt per eligible owner, without resource cleanup,
  // allocation or release. Original results are still published by submit().
  // A second (including reentrant) notifier returns EBUSY without waiting;
  // maintenance may run concurrently, including inside notification methods.
  // Gate-owned cursors follow only the initial chains, with unlink repairing
  // them before storage can disappear. EAGAIN/EBUSY retries wait for a new pass.
  LIBC_INLINE int dispatch_notifications() {
    {
      StateGuard guard(gate);
      if (dispatching)
        return EBUSY;
      dispatching = true;
      notification_group = list_groups;
      notification_record = records;
    }
    int error = 0;
    for (;;) {
      ListGroup *group;
      bool rollback;
      {
        StateGuard guard(gate);
        group = notification_group;
        if (!group)
          break;
        notification_group = group->next;
        if (group->phase == ListGroup::Phase::Preparing ||
            group->notification_terminal || group->notification_busy)
          continue;
        rollback = group->phase == ListGroup::Phase::Rollback;
        if (!rollback && !group->complete)
          continue;
        group->notification_busy = true;
      }
      int result = 0;
      if (rollback)
        group->notification.discard();
      else
        result = int(group->notification.activate());
      {
        StateGuard guard(gate);
        group->notification_terminal = result != EAGAIN && result != EBUSY;
        group->notification_busy = false;
      }
      // The owner may already have been destroyed after releasing the gate.
      if (!error)
        error = result;
    }
    for (;;) {
      Record *record;
      bool discard;
      {
        StateGuard guard(gate);
        record = notification_record;
        if (!record)
          break;
        notification_record = record->next;
        if (record->phase == Phase::Preparing ||
            record->notification_terminal || record->notification_busy)
          continue;
        discard = record->phase == Phase::Rollback ||
                  record->phase == Phase::Rejected;
        if (!discard && !record->notification_claimed) {
          if (!record->request.complete())
            continue;
          if (!record->request.state().take_notification())
            __builtin_trap();
          record->notification_claimed = true;
        }
        record->notification_busy = true;
      }
      int result = 0;
      if (discard)
        record->notification.discard();
      else
        result = int(record->notification.activate());
      {
        StateGuard guard(gate);
        record->notification_terminal = result != EAGAIN && result != EBUSY;
        record->notification_busy = false;
      }
      if (!error)
        error = result;
    }
    {
      StateGuard guard(gate);
      dispatching = false;
    }
    return error;
  }

  // Reclamation only: never activate or discard a notification. One pass, no
  // waiting for callbacks or retry-until-success loops. A second
  // (including reentrant) maintainer returns EBUSY without waiting. Returns the
  // first diagnostic; neither success nor failure authorizes destruction of a
  // nonempty service. Call again for retained cleanup/release errors.
  LIBC_INLINE int maintain() {
    Record *record;
    CancelBatch *batch;
    ListGroup *group;
    {
      StateGuard guard(gate);
      if (maintaining)
        return EBUSY;
      maintaining = true;
      record = records;
      batch = cancel_batches;
      group = list_groups;
    }
    int error = 0;
    while (group) {
      ListGroup *next;
      {
        StateGuard guard(gate);
        next = group->next;
      }
      int result = maintain_list(*group);
      if (!error)
        error = result;
      group = next;
    }
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
