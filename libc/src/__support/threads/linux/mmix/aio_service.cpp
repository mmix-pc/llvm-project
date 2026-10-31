//===-- MMIX process AIO service ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "aio_service.h"
#include "aio_dispatcher.h"
#include "aio_service_registry.h"
#include "aio_transfer.h"
#include "cancellation.h"
#include "cancellation_point.h"
#include "config/app.h"
#include "hdr/pthread_macros.h"
#include "hdr/time_macros.h"
#include "src/stdlib/linux/mmix/allocator.h"
#include "src/setjmp/mmix/linux/jump_cleanup.h"
#include <asm/rstack.h>
#include <pthread.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {
struct Allocator {
  void *allocate(size_t size, size_t alignment) {
    return mmix::allocate(size, alignment);
  }
  void deallocate(void *pointer) { mmix::deallocate(pointer); }
};
using Registry = AioServiceRegistry<Allocator, 16, io_uring::Ring<>,
                                    aio_submission_resources::LinuxOperations,
                                    AioNotification, AioGate, aiocb>;
struct Service {
  const long pid;
  io_uring::Ring<> ring;
  Allocator allocator;
  uint64_t watermark = 0;
  Registry registry;
  AioDispatcher dispatcher;
  bool initialized = false;
  bool running = false;
  explicit Service(long pid)
      : pid(pid), registry(ring, allocator, {}, watermark, uint64_t(pid)) {}
  static bool drained(void *context) {
    auto &self = *static_cast<Service *>(context);
    return !self.registry.retained() && !self.registry.has_cancel_batches() &&
           !self.registry.has_list_groups();
  }
  static int progress(void *context) {
    auto &self = *static_cast<Service *>(context);
    int error = self.registry.submit();
    int cleanup = self.registry.maintain();
    return error ? error : cleanup;
  }
};

// Permanent storage has no exit destructor. Publication is never reclaimed
// while a signal-safe query could have loaded its address. A fork child may
// reconstruct ONLY a fully drained object; it has no surviving other caller.
alignas(Service) unsigned char storage[sizeof(Service)];
Service *service = nullptr;
LIBC_CONSTINIT cpp::Atomic<Service *> published{nullptr};
LIBC_CONSTINIT cpp::Atomic<long> owner_pid{0};
struct alignas(8) Serializer {
  RawMutex mutex;
};
LIBC_CONSTINIT Serializer serializer;
LIBC_CONSTINIT cpp::Atomic<uint64_t> closed{0};
bool starting = false;
bool checkpoint = false;
bool checkpoint_empty = false;
bool quarantined = false;

class Call {
  uint64_t saved_mask = 0;
  int saved_cancel = PTHREAD_CANCEL_DISABLE;
  bool masked = false, disabled = false, locked = false;
  int error_code = 0;

public:
  Call() {
    auto *caller = current_control;
    if (!caller || !caller->fork_recovery.allows_thread_creation()) {
      error_code = ENOTSUP;
      return;
    }
    uint64_t blocked = UINT64_MAX & ~CREDENTIAL_MASK;
    long result = syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK, &blocked,
                                     &saved_mask, sizeof(saved_mask));
    if (result) {
      error_code = int(-result);
      return;
    }
    masked = true;
    mmix_rstack_query query{};
    result = syscall_impl<long>(__NR_mmix_rstack_query, &query);
    if (result || !caller->unwind_root.active ||
        query.chain_id != caller->unwind_root.chain) {
      error_code = result ? int(-result) : ENOTSUP;
      return;
    }
    error_code = set_cancel_state(PTHREAD_CANCEL_DISABLE, &saved_cancel);
    if (error_code)
      return;
    disabled = true;
    long pid = owner_pid.load(cpp::MemoryOrder::ACQUIRE);
    if (pid && pid != syscall_impl<long>(SYS_getpid)) {
      error_code = ENOTSUP;
      return;
    }
    lock();
    if (starting || checkpoint || quarantined ||
        closed.load(cpp::MemoryOrder::ACQUIRE) ||
        !thread_registry.admit_signal_notification())
      error_code = EAGAIN;
    // Minimal fork may bypass the coordinated hook. Never use a copied ring
    // or destroy its records just because the copied serializer is available.
    if (!error_code && service &&
        service->pid != syscall_impl<long>(SYS_getpid))
      error_code = ENOTSUP;
  }
  ~Call() noexcept(false) {
    if (locked)
      unlock();
    if (masked && syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK,
                                     &saved_mask, nullptr, sizeof(saved_mask)))
      __builtin_trap();
    if (disabled && set_cancel_state(saved_cancel, nullptr))
      __builtin_trap();
  }
  int error() const { return error_code; }
  void lock() {
    if (!serializer.mutex.lock())
      __builtin_trap();
    locked = true;
  }
  void unlock() {
    if (!locked || !serializer.mutex.unlock())
      __builtin_trap();
    locked = false;
  }
};

int ensure_service(Call &call) {
  if (!service) {
    long pid = syscall_impl<long>(SYS_getpid);
    if (pid <= 0)
      return pid ? int(-pid) : EPROTO;
    owner_pid.store(pid, cpp::MemoryOrder::RELEASE);
    service = new (storage) Service(pid);
  }
  if (service->running)
    return 0;
  starting = true;
  call.unlock();
  auto &owner = *service;
  // Failed startup retains cleanup ownership. Retry it before starting again,
  // without holding the publication mutex over loader/thread operations.
  int error = owner.dispatcher.stop();
  if (!error && !owner.initialized) {
    auto reset = owner.ring.reset();
    if (!reset)
      error = reset.error();
    if (!error) {
      auto ready = owner.ring.initialize(256);
      if (!ready)
        error = ready.error();
    }
    if (!error) {
      auto ready = owner.registry.initialize(256);
      if (!ready)
        error = ready.error();
      else
        owner.initialized = true;
    }
  }
  if (!error)
    error =
        owner.dispatcher.start(app.tls, app.page_size, owner.ring.descriptor(),
                               {&owner, Service::progress, Service::drained});
  call.lock();
  starting = false;
  if (!error) {
    owner.running = true;
    published.store(&owner, cpp::MemoryOrder::RELEASE);
  }
  return error;
}

Service *query_service() {
  auto *owner = published.load(cpp::MemoryOrder::ACQUIRE);
  return owner && owner->pid == syscall_impl<long>(SYS_getpid) ? owner
                                                               : nullptr;
}

// Protect the operation pin too, not only the registry's inner state gate.
// A handler longjmp between pin acquisition and gate entry must not strand a
// permanent activity count, and a handler-side _Fork must not resume a query
// whose process identity was checked only in its parent.
class QueryMask {
  uint64_t saved = 0;
  int error_code = 0;

public:
  QueryMask() {
    uint64_t blocked = UINT64_MAX & ~CREDENTIAL_MASK;
    long result = syscall_impl<long>(SYS_rt_sigprocmask, SIG_BLOCK, &blocked,
                                     &saved, sizeof(saved));
    if (result)
      error_code = int(-result);
  }
  ~QueryMask() {
    if (!error_code && syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK,
                                          &saved, nullptr, sizeof(saved)))
      __builtin_trap();
  }
  int error() const { return error_code; }
};

// The only automatically owned state across the wait is trivially destructible.
// A compiler cleanup supplies the exception landing; paired jump hooks handle
// legal nonlocal exits without requiring a C++ destructor or catch runtime.
struct WaitFrame {
  using Operation = AioDispatcher::Operation;
  using Node = JumpCleanupStack::Node;
  Service &owner;
  Registry::Wait wait;
  alignas(Operation) unsigned char operation_storage[sizeof(Operation)];
  alignas(Node) unsigned char pin_storage[sizeof(Node)];
  Operation *operation = nullptr;
  Node *pin = nullptr;
  bool waiting = false;

  explicit WaitFrame(Service &owner)
      : owner(owner), wait(owner.registry.make_wait(jump_cleanup_head)) {}
  static bool release_pin(void *context) {
    auto &self = *static_cast<WaitFrame *>(context);
    if (!self.operation)
      return false;
    self.operation->~Operation();
    self.operation = nullptr;
    return true;
  }
  // Caller blocks application/cancellation signals until both nodes are live.
  int begin(const aiocb *const *list, size_t count) {
    operation = new (operation_storage) Operation(owner.dispatcher);
    if (!*operation) {
      operation->~Operation();
      operation = nullptr;
      return EAGAIN;
    }
    pin = new (pin_storage) Node;
    if (!JumpCleanupStack::push(jump_cleanup_head, *pin, release_pin, this))
      __builtin_trap();
    int error = owner.registry.begin_wait(wait, list, count);
    waiting = !error;
    return error;
  }
  static void finish(WaitFrame *self) {
    QueryMask mask;
    if (mask.error())
      __builtin_trap();
    if (self->waiting) {
      if (self->wait.finish())
        __builtin_trap();
      self->waiting = false;
    }
    if (self->pin) {
      if (!JumpCleanupStack::pop(jump_cleanup_head, *self->pin))
        __builtin_trap();
      self->pin->~Node();
      self->pin = nullptr;
    }
  }
};
static_assert(__is_trivially_destructible(WaitFrame));
void finish_wait_frame(WaitFrame *frame) { WaitFrame::finish(frame); }

struct ListFrame {
  using Operation = AioDispatcher::Operation;
  using Node = JumpCleanupStack::Node;
  Service *owner = nullptr;
  Registry::ListGroup *group = nullptr;
  alignas(Operation) unsigned char operation_storage[sizeof(Operation)];
  alignas(Node) unsigned char pin_storage[sizeof(Node)];
  Operation *operation = nullptr;
  Node *pin = nullptr;

  static bool release(void *context) {
    auto &self = *static_cast<ListFrame *>(context);
    if (!self.operation)
      return false;
    if (self.group) {
      if (self.owner->registry.finish_list(*self.group))
        return false;
      self.group = nullptr;
    }
    self.operation->~Operation();
    self.operation = nullptr;
    return true;
  }
  // Preparation and publication run with application/cancellation signals off.
  int begin(Service &service) {
    owner = &service;
    operation = new (operation_storage) Operation(owner->dispatcher);
    if (!*operation) {
      operation->~Operation();
      operation = nullptr;
      return EAGAIN;
    }
    pin = new (pin_storage) Node;
    if (!JumpCleanupStack::push(jump_cleanup_head, *pin, release, this))
      __builtin_trap();
    return 0;
  }
  int result() {
    QueryMask mask;
    if (mask.error())
      return mask.error();
    auto status = owner->registry.list_result(*group);
    return status ? *status : status.error();
  }
  static void finish(ListFrame *self) {
    QueryMask mask;
    if (mask.error())
      __builtin_trap();
    if (self->pin) {
      if (!JumpCleanupStack::pop(jump_cleanup_head, *self->pin))
        __builtin_trap();
      self->pin->~Node();
      self->pin = nullptr;
    }
  }
};
static_assert(__is_trivially_destructible(ListFrame));
void finish_list_frame(ListFrame *frame) { ListFrame::finish(frame); }

int cancel_wait(void *, Futex *word, int operation, FutexWordType expected,
                const void *timeout, unsigned bitset) {
  return int(cancellation_syscall(FUTEX_SYSCALL_ID,
                                  reinterpret_cast<long>(word), operation,
                                  expected, reinterpret_cast<long>(timeout),
                                  0, bitset));
}
} // namespace

int submit_aio_transfer(const AioInput &input) {
  if (!input.control_block || input.member_error ||
      (input.opcode != LIO_READ && input.opcode != LIO_WRITE))
    return EINVAL;
  Call call;
  if (call.error())
    return call.error();
  if (int error = ensure_service(call))
    return error;
  AioDispatcher::Operation operation(service->dispatcher);
  if (!operation)
    return EAGAIN;
  call.unlock();
  auto result = service->registry.admit(
      {input.control_block, input.descriptor,
       input.opcode == LIO_READ ? Registry::Engine::Operation::Read
                                : Registry::Engine::Operation::Write,
       const_cast<void *>(input.buffer), 0, 0, false},
      [&](AioNotification &event) {
        return event.prepare(input.event, app.tls, app.page_size);
      },
      [&](int retained, Registry::Input &normalized) {
        AioTransfer transfer{};
        int error = normalize_aio_transfer(retained, input, transfer);
        if (!error) {
          normalized.size = transfer.size;
          normalized.offset = transfer.offset;
          normalized.transfer_flags = transfer.flags;
        }
        return error;
      });
  // Wake also drives preparation rollback. Failure cannot revoke accepted I/O;
  // the helper's bounded retry interval continues to make progress.
  (void)service->dispatcher.wake();
  return result ? 0 : result.error();
}

int submit_aio_sync(const AioSyncInput &input) {
  if (!input.control_block)
    return EINVAL;
  Call call;
  if (call.error())
    return call.error();
  if (int error = ensure_service(call))
    return error;
  AioDispatcher::Operation operation(service->dispatcher);
  if (!operation)
    return EAGAIN;
  call.unlock();
  auto result = service->registry.admit(
      {input.control_block, input.descriptor,
       input.data_only ? Registry::Engine::Operation::DataSync
                       : Registry::Engine::Operation::Sync,
       nullptr, 0, 0, false},
      [&](AioNotification &event) {
        return event.prepare(input.event, app.tls, app.page_size);
      });
  (void)service->dispatcher.wake();
  return result ? 0 : result.error();
}

namespace {
int prepare_aio_list(ListFrame &frame, int mode, aiocb *const *list, int count,
                     const sigevent *notification) {
  Call call;
  if (call.error())
    return call.error();
  // Capture and allocation run outside the publication serializer. The
  // operation pin excludes fork throughout preparation and ownership release.
  if (int error = ensure_service(call))
    return error;
  if (int error = frame.begin(*service))
    return error;
  call.unlock();
  Allocator allocator;
  AioListInput<Allocator> captured(allocator);
  if (int error = captured.capture(mode, list, count, notification))
    return error;
  size_t size = captured.size();
  if (size > SIZE_MAX / sizeof(Registry::ListInput))
    return ENOMEM;
  Registry::ListInput *inputs = nullptr;
  if (size) {
    inputs = static_cast<Registry::ListInput *>(allocator.allocate(
        size * sizeof(Registry::ListInput), alignof(Registry::ListInput)));
    if (!inputs)
      return ENOMEM;
  }
  for (size_t i = 0; i < size; ++i) {
    const auto &input = captured.data()[i];
    new (&inputs[i]) Registry::ListInput{
        {input.control_block, input.descriptor,
         input.opcode == LIO_READ ? Registry::Engine::Operation::Read
                                  : Registry::Engine::Operation::Write,
         const_cast<void *>(input.buffer), 0, 0, false},
        input.member_error};
  }
  auto prepared = service->registry.prepare_list(
      inputs, size, [&](AioNotification &event) {
        return event.prepare(captured.notification(), app.tls, app.page_size);
      });
  for (size_t i = 0; i < size; ++i)
    inputs[i].~ListInput();
  if (inputs)
    allocator.deallocate(inputs);
  if (!prepared) {
    (void)service->dispatcher.wake();
    return prepared.error();
  }
  auto &group = **prepared;
  int error = service->registry.start_list(
      group,
      [&](size_t i, AioNotification &event) {
        return event.prepare(captured.data()[i].event, app.tls, app.page_size);
      },
      [&](size_t i, int retained, Registry::Input &normalized) {
        AioTransfer transfer{};
        int result = normalize_aio_transfer(retained, captured.data()[i], transfer);
        if (!result) {
          normalized.size = transfer.size;
          normalized.offset = transfer.offset;
          normalized.transfer_flags = transfer.flags;
        }
        return result;
      });
  frame.group = &group;
  (void)service->dispatcher.wake();
  return error;
}

[[gnu::noinline, clang::disable_tail_calls]]
int submit_aio_list(int mode, aiocb *const *list, int count,
                    const sigevent *notification,
                    AioWaitEvent::WaitSyscall wait_syscall, void *context) {
  if (count < 0 || (count && !list))
    return EINVAL;
  uint64_t saved_mask;
  const uint64_t blocked = UINT64_MAX & ~CREDENTIAL_MASK;
  long masked = syscall_impl<long>(SYS_rt_sigprocmask, SIG_BLOCK, &blocked,
                                   &saved_mask, sizeof(saved_mask));
  if (masked)
    return int(-masked);
  ListFrame frame __attribute__((cleanup(finish_list_frame)));
  // Destroy all capture scratch and Call before permitting a handler jump.
  // The frame's cleanup is already registered when Call restores cancellation.
  int error = prepare_aio_list(frame, mode, list, count, notification);
  if (mode == LIO_NOWAIT || !frame.group)
    ListFrame::finish(&frame);
  if (syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK, &saved_mask, nullptr,
                         sizeof(saved_mask)))
    __builtin_trap();
  if (mode == LIO_NOWAIT || !frame.group)
    return error;
  // Even an already-complete group checks pending cancellation with its caller
  // owner protected. A start error does not imply accepted siblings are done.
  test_cancel();
  error = frame.owner->registry.wait_list(
      *frame.group, nullptr, wait_syscall ? wait_syscall : cancel_wait, context);
  return error ? error : frame.result();
}
} // namespace

int submit_aio_list_nowait(aiocb *const *list, int count,
                           const sigevent *notification) {
  return submit_aio_list(LIO_NOWAIT, list, count, notification, nullptr, nullptr);
}

int submit_aio_list_wait(aiocb *const *list, int count,
                         const sigevent *notification,
                         AioWaitEvent::WaitSyscall wait_syscall, void *context) {
  return submit_aio_list(LIO_WAIT, list, count, notification, wait_syscall,
                         context);
}

ErrorOr<int> query_aio_error(const aiocb *cb) {
  QueryMask mask;
  if (mask.error())
    return Error(mask.error());
  auto *owner = query_service();
  if (!owner || !cb)
    return Error(EINVAL);
  AioDispatcher::Operation operation(owner->dispatcher);
  return operation ? owner->registry.error(cb) : ErrorOr<int>(Error(EAGAIN));
}

ErrorOr<AioRequest::Result> take_aio_result(const aiocb *cb) {
  QueryMask mask;
  if (mask.error())
    return Error(mask.error());
  auto *owner = query_service();
  if (!owner || !cb)
    return Error(EINVAL);
  AioDispatcher::Operation operation(owner->dispatcher);
  if (!operation)
    return Error(EAGAIN);
  // No wake syscall is required here. The helper's periodic pass reclaims the
  // released record; result retrieval remains allocation/loader/ring-lock free.
  return owner->registry.take_result(cb);
}

ErrorOr<int> cancel_aio_requests(int descriptor, const aiocb *cb) {
  Call call;
  if (call.error())
    return Error(call.error());
  long flags = syscall_impl<long>(SYS_fcntl, descriptor, F_GETFD);
  if (flags < 0)
    return Error(int(-flags));
  // Cancellation of an empty descriptor must not allocate a ring or helper.
  if (!service || !service->running)
    return cb ? ErrorOr<int>(Error(EINVAL)) : ErrorOr<int>(AIO_ALLDONE);
  AioDispatcher::Operation operation(service->dispatcher);
  if (!operation)
    return Error(EAGAIN);
  call.unlock();
  auto prepared = service->registry.prepare_cancels(descriptor, cb);
  if (!prepared)
    return Error(prepared.error() == ERANGE ? EAGAIN : prepared.error());
  auto &batch = **prepared;
  int attempted = service->registry.attempt_cancels(batch);
  auto status = service->registry.cancel_status(batch);
  if (service->registry.finish_cancels(batch))
    __builtin_trap();
  (void)service->dispatcher.wake();
  if (!status)
    return Error(status.error());
  // Even a failed control reservation does not undo successful sibling
  // attempts. Keep their observations in the service until real completion.
  if (attempted && *status == Registry::CancelStatus::NotCanceled)
    return Error(attempted);
  switch (*status) {
  case Registry::CancelStatus::AllDone:
    return AIO_ALLDONE;
  case Registry::CancelStatus::Canceled:
    return AIO_CANCELED;
  case Registry::CancelStatus::NotCanceled:
    return AIO_NOTCANCELED;
  }
  __builtin_trap();
}

namespace {
[[gnu::noinline, clang::disable_tail_calls]]
int suspend_aio_requests_impl(const aiocb *const *list, size_t count,
                              const timespec *timeout, bool relative,
                              AioWaitEvent::WaitSyscall wait_syscall,
                              void *context) {
  uint64_t saved_mask;
  const uint64_t blocked = UINT64_MAX & ~CREDENTIAL_MASK;
  long masked = syscall_impl<long>(SYS_rt_sigprocmask, SIG_BLOCK, &blocked,
                                   &saved_mask, sizeof(saved_mask));
  if (masked)
    return int(-masked);
  auto *owner = query_service();
  if (!owner) {
    if (syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK, &saved_mask,
                           nullptr, sizeof(saved_mask)))
      __builtin_trap();
    return EINVAL;
  }
  WaitFrame frame __attribute__((cleanup(finish_wait_frame)))(*owner);
  int error = frame.begin(list, count);
  if (error)
    WaitFrame::finish(&frame);
  if (syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK, &saved_mask, nullptr,
                         sizeof(saved_mask)))
    __builtin_trap();
  if (error)
    return error;
  auto &event = frame.wait.completion();
  auto wait = wait_syscall ? wait_syscall : cancel_wait;
  if (relative) {
    return aio_service_detail::wait_relative(
        event, timeout,
        [](timespec &now) {
          static_assert(sizeof(timespec::tv_sec) == 8 &&
                        sizeof(timespec::tv_nsec) == 8 && sizeof(timespec) == 16);
          // Match the MMIX syscall-only clock provider without introducing
          // public errno, lazy vDSO resolution or cancellation into sampling.
          long result =
              syscall_impl<long>(SYS_clock_gettime, CLOCK_MONOTONIC, &now);
          return result < 0 ? int(-result) : result ? EIO : 0;
        },
        wait, context);
  }
  return event.wait(timeout, wait, context);
}
} // namespace

int suspend_aio_requests(const aiocb *const *list, size_t count,
                         const timespec *deadline,
                         AioWaitEvent::WaitSyscall wait_syscall, void *context) {
  return suspend_aio_requests_impl(list, count, deadline, false, wait_syscall,
                                   context);
}

int suspend_aio_requests_relative(const aiocb *const *list, size_t count,
                                  const timespec *timeout,
                                  AioWaitEvent::WaitSyscall wait_syscall,
                                  void *context) {
  return suspend_aio_requests_impl(list, count, timeout, true, wait_syscall,
                                   context);
}

int prepare_aio_fork(bool minimal, bool *drained) {
  *drained = false;
  if (!serializer.mutex.try_lock())
    return EAGAIN;
  if (starting || checkpoint || quarantined) {
    serializer.mutex.unlock();
    return EAGAIN;
  }
  if (service && !service->dispatcher.try_park()) {
    serializer.mutex.unlock();
    return EAGAIN;
  }
  checkpoint = true;
  checkpoint_empty = false;
  serializer.mutex.unlock();
  int error = 0;
  if (service && Service::drained(service)) {
    if (minimal && !service->dispatcher.drained())
      error = EAGAIN;
    else
      error = service->dispatcher.stop_parked();
    if (!error)
      service->running = false;
    // Complete empty-block disposal before snapshotting allocator state. The
    // parked worker and excluded submitters cannot attach new storage here.
    if (!error)
      error = service->registry.maintain();
    if (!error)
      checkpoint_empty = true;
  }
  if (error) {
    finish_aio_fork(false);
    return error;
  }
  *drained = !service || service->dispatcher.drained();
  return 0;
}

void finish_aio_fork(bool child) {
  if (!checkpoint)
    __builtin_trap();
  // The checkpoint excludes startup and all transactions. Do not acquire
  // copied registry/ring gates or invoke notification/allocator teardown.
  if (child && service) {
    published.store(nullptr, cpp::MemoryOrder::RELEASE);
    bool empty = checkpoint_empty;
    if (!empty) {
      service->dispatcher.quarantine_parked_child();
      (void)service->registry.quarantine_child();
    }
    auto reset = service->ring.reset();
    if (empty && reset) {
      service->dispatcher.resume();
      service->~Service();
      service = nullptr;
      owner_pid.store(0, cpp::MemoryOrder::RELEASE);
    } else {
      quarantined = true;
    }
  } else if (service) {
    service->dispatcher.resume();
  }
  if (!serializer.mutex.lock())
    __builtin_trap();
  if (child)
    closed.store(0, cpp::MemoryOrder::RELEASE);
  checkpoint = false;
  if (!serializer.mutex.unlock())
    __builtin_trap();
}

void close_aio_admission() { closed.store(1, cpp::MemoryOrder::RELEASE); }

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
