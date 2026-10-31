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
#include "config/app.h"
#include "hdr/pthread_macros.h"
#include "src/stdlib/linux/mmix/allocator.h"
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
