//===-- Linux io_uring transport -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_IO_URING_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_IO_URING_H

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/stdint_proxy.h"
#include "hdr/sys_mman_macros.h"
#include "hdr/types/off_t.h"
#include "src/__support/CPP/atomic.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/mmap.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"
#include <linux/io_uring.h>
#include <stddef.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace io_uring {

static_assert(sizeof(io_uring_sqe) == 64 && sizeof(io_uring_cqe) == 16);
static_assert(offsetof(io_uring_sqe, user_data) == 32);
static_assert(sizeof(io_uring_sqe::user_data) == sizeof(uint64_t));

// Operations return checked values or positive errno, never touching
// libc_errno. map creates a shared read/write mapping; unmap and close make one
// attempt. A replacement Operations object must obey the same ownership
// contracts and keep its backing state alive until the Ring is destroyed.
// Failed unmap retains mapping ownership; close consumes the fd even on error.
struct LinuxOperations {
  LIBC_INLINE ErrorOr<int> setup(unsigned entries, io_uring_params &params) {
    return linux_syscalls::syscall_checked<int>(SYS_io_uring_setup, entries,
                                                &params);
  }
  LIBC_INLINE ErrorOr<int> descriptor_flags(int fd) {
#ifdef SYS_fcntl
    constexpr long NUMBER = SYS_fcntl;
#else
    constexpr long NUMBER = SYS_fcntl64;
#endif
    return linux_syscalls::syscall_checked<int>(NUMBER, fd, F_GETFD, 0);
  }
  LIBC_INLINE ErrorOr<void *> map(int fd, size_t size, off_t offset) {
    return linux_syscalls::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                                MAP_SHARED, fd, offset);
  }
  LIBC_INLINE ErrorOr<int> unmap(void *address, size_t size) {
    return linux_syscalls::syscall_checked<int>(SYS_munmap, address, size);
  }
  LIBC_INLINE ErrorOr<int> close(int fd) {
    return linux_syscalls::syscall_checked<int>(SYS_close, fd);
  }
  LIBC_INLINE ErrorOr<unsigned> enter(int fd, unsigned submit, unsigned wait,
                                      unsigned flags) {
    return linux_syscalls::syscall_checked<unsigned>(
        SYS_io_uring_enter, fd, submit, wait, flags, nullptr, 0);
  }
  LIBC_INLINE ErrorOr<int> register_operation(int fd, unsigned opcode,
                                             const void *argument,
                                             unsigned count) {
    return linux_syscalls::syscall_checked<int>(SYS_io_uring_register, fd,
                                                opcode, argument, count);
  }
};

// Owns only the ring fd and mappings, not request buffers or request policy.
// Serialize enqueue/submit with each other, and poll/wait with each other. One
// producer and one consumer may run concurrently; initialize/reset/destruction
// require exclusive access. Callers must keep request resources alive until
// completion: closing a ring is not a synchronous cancellation/join operation.
// Errors are positive errno. EPROTO means corrupt geometry/indices, EIO dropped
// SQ entries, EOVERFLOW lost CQEs. These require owner recovery, not retries.
template <class Operations = LinuxOperations> class Ring {
  struct Mapping {
    void *address = nullptr;
    size_t size = 0;
    bool owned = false;
  };
  struct Region {
    size_t offset;
    size_t size;
    size_t alignment;
  };

  Operations operations;
  int fd = -1;
  io_uring_params params = {};
  Mapping sq, cq, sqes;
  bool ready = false;

  LIBC_INLINE bool owns_mappings() const {
    return sq.owned || cq.owned || sqes.owned;
  }

  LIBC_INLINE static __u32 acquire(__u32 *word) {
    return cpp::AtomicRef<__u32>(word).load(cpp::MemoryOrder::ACQUIRE,
                                            cpp::MemoryScope::SYSTEM);
  }
  LIBC_INLINE static void release(__u32 *word, __u32 value) {
    cpp::AtomicRef<__u32>(word).store(value, cpp::MemoryOrder::RELEASE,
                                      cpp::MemoryScope::SYSTEM);
  }
  template <class T>
  LIBC_INLINE static T *at(const Mapping &mapping, size_t offset) {
    return reinterpret_cast<T *>(static_cast<char *>(mapping.address) + offset);
  }
  LIBC_INLINE __u32 *sq_word(__u32 offset) const {
    return at<__u32>(sq, offset);
  }
  LIBC_INLINE __u32 *cq_word(__u32 offset) const {
    return at<__u32>(cq, offset);
  }

  LIBC_INLINE static bool capacity(__u32 count) {
    return count && count < (__u32(1) << 31) && !(count & (count - 1));
  }
  LIBC_INLINE static bool extent(size_t offset, size_t count, size_t width,
                                 size_t &size) {
    size_t bytes;
    return !__builtin_mul_overflow(count, width, &bytes) &&
           !__builtin_add_overflow(offset, bytes, &size) && size &&
           size <= PTRDIFF_MAX;
  }
  LIBC_INLINE static bool overlaps(const Region &a, const Region &b) {
    return a.offset < b.offset + b.size && b.offset < a.offset + a.size;
  }
  template <size_t N>
  LIBC_INLINE static bool valid_regions(const Region (&regions)[N],
                                        size_t size) {
    for (size_t i = 0; i < N; ++i) {
      const Region &r = regions[i];
      if (r.offset % r.alignment || r.offset > size || r.size > size - r.offset)
        return false;
      for (size_t j = 0; j < i; ++j)
        if (overlaps(r, regions[j]))
          return false;
    }
    return true;
  }

  LIBC_INLINE ErrorOr<void> layout(unsigned requested) {
    if (params.flags || !capacity(params.sq_entries) ||
        !capacity(params.cq_entries) || params.sq_entries < requested ||
        params.cq_entries < params.sq_entries)
      return Error(EPROTO);
    if (!extent(params.sq_off.array, params.sq_entries, sizeof(__u32),
                sq.size) ||
        !extent(params.cq_off.cqes, params.cq_entries, sizeof(io_uring_cqe),
                cq.size) ||
        !extent(0, params.sq_entries, sizeof(io_uring_sqe), sqes.size))
      return Error(EOVERFLOW);

    const auto &s = params.sq_off;
    const auto &c = params.cq_off;
    const Region sr[] = {
        {s.head, sizeof(__u32), alignof(__u32)},
        {s.tail, sizeof(__u32), alignof(__u32)},
        {s.ring_mask, sizeof(__u32), alignof(__u32)},
        {s.ring_entries, sizeof(__u32), alignof(__u32)},
        {s.flags, sizeof(__u32), alignof(__u32)},
        {s.dropped, sizeof(__u32), alignof(__u32)},
        {s.array, sq.size - s.array, alignof(__u32)},
    };
    const Region cr[] = {
        {c.head, sizeof(__u32), alignof(__u32)},
        {c.tail, sizeof(__u32), alignof(__u32)},
        {c.ring_mask, sizeof(__u32), alignof(__u32)},
        {c.ring_entries, sizeof(__u32), alignof(__u32)},
        {c.overflow, sizeof(__u32), alignof(__u32)},
        {c.cqes, cq.size - c.cqes, alignof(io_uring_cqe)},
    };
    if (!valid_regions(sr, sq.size) || !valid_regions(cr, cq.size))
      return Error(EPROTO);
    if (params.features & IORING_FEAT_SINGLE_MMAP) {
      for (const auto &a : sr)
        for (const auto &b : cr)
          if (overlaps(a, b))
            return Error(EPROTO);
      sq.size = cq.size = sq.size > cq.size ? sq.size : cq.size;
    }
    return {};
  }

  LIBC_INLINE ErrorOr<void> map(Mapping &mapping, off_t offset,
                                size_t alignment) {
    auto result = operations.map(fd, mapping.size, offset);
    if (!result)
      return Error(result.error());
    mapping.address = *result;
    mapping.owned = true;
    uintptr_t address = reinterpret_cast<uintptr_t>(*result);
    // Remember even a null/invalid successful mapping so rollback unmaps it.
    if (!address || address % alignment || mapping.size > UINTPTR_MAX - address)
      return Error(EPROTO);
    return {};
  }

  LIBC_INLINE ErrorOr<void> health() const {
    if (!ready)
      return Error(EBADF);
    if (acquire(sq_word(params.sq_off.ring_mask)) != params.sq_entries - 1 ||
        acquire(sq_word(params.sq_off.ring_entries)) != params.sq_entries ||
        acquire(cq_word(params.cq_off.ring_mask)) != params.cq_entries - 1 ||
        acquire(cq_word(params.cq_off.ring_entries)) != params.cq_entries)
      return Error(EPROTO);
    // NODROP queues overflow in the kernel, but allocation failure can still
    // lose CQEs. Never turn a nonzero loss counter into an apparently empty CQ.
    if (acquire(cq_word(params.cq_off.overflow)))
      return Error(EOVERFLOW);
    if (acquire(sq_word(params.sq_off.dropped)))
      return Error(EIO);
    return {};
  }

  LIBC_INLINE ErrorOr<void> take_completion(io_uring_cqe &completion) {
    auto status = health();
    if (!status)
      return Error(status.error());
    __u32 head = acquire(cq_word(params.cq_off.head));
    __u32 count = acquire(cq_word(params.cq_off.tail)) - head;
    if (count > params.cq_entries)
      return Error(EPROTO);
    if (!count)
      return Error(EAGAIN);
    completion = at<io_uring_cqe>(
        cq, params.cq_off.cqes)[head & (params.cq_entries - 1)];
    release(cq_word(params.cq_off.head), head + 1);
    return {};
  }

  LIBC_INLINE ErrorOr<void> fail(int error) {
    (void)reset();
    return Error(error);
  }

public:
  // Constructs a closed ring. The operations object is copied, not owned by
  // reference; injected external backing storage remains the caller's property.
  LIBC_INLINE explicit Ring(Operations operations = {})
      : operations(operations) {}
  Ring(const Ring &) = delete;
  Ring &operator=(const Ring &) = delete;
  Ring(Ring &&) = delete;
  Ring &operator=(Ring &&) = delete;
  // Attempts cleanup once, including retained mappings. Failure to release a
  // mapping traps rather than silently losing ownership. Close errors alone
  // cannot be retried and do not trap; use reset to observe them.
  LIBC_INLINE ~Ring() {
    (void)reset();
    if (owns_mappings())
      __builtin_trap();
  }

  // Creates a real kernel ring with all setup flags zero (no SQPOLL/IOPOLL).
  // Zero entries is EINVAL; an open ring or retained mapping is EBUSY. No
  // fallback/retries. Failure attempts rollback and preserves the original
  // initialization error; failed unmaps remain owned for a later reset.
  LIBC_INLINE ErrorOr<void> initialize(unsigned entries) {
    if (fd >= 0 || owns_mappings())
      return Error(EBUSY);
    if (!entries)
      return Error(EINVAL);
    params = {};
    auto created = operations.setup(entries, params);
    if (!created)
      return Error(created.error());
    fd = *created;
    if (fd < 0)
      return fail(EPROTO);
    // io_uring_setup allocates with O_CLOEXEC atomically. Audit that contract;
    // setting it afterward would introduce an exec inheritance window.
    auto flags = operations.descriptor_flags(fd);
    if (!flags)
      return fail(flags.error());
    if (!(*flags & FD_CLOEXEC))
      return fail(EPROTO);
    auto status = layout(entries);
    if (!status)
      return fail(status.error());
    status = map(sq, IORING_OFF_SQ_RING, alignof(__u32));
    if (!status)
      return fail(status.error());
    if (params.features & IORING_FEAT_SINGLE_MMAP) {
      cq.address = sq.address;
      if (reinterpret_cast<uintptr_t>(cq.address) % alignof(io_uring_cqe))
        return fail(EPROTO);
    } else {
      status = map(cq, IORING_OFF_CQ_RING, alignof(io_uring_cqe));
      if (!status)
        return fail(status.error());
    }
    status = map(sqes, IORING_OFF_SQES, alignof(io_uring_sqe));
    if (!status)
      return fail(status.error());
    ready = true;
    status = health();
    if (!status)
      return fail(status.error());
    if (acquire(sq_word(params.sq_off.head)) ||
        acquire(sq_word(params.sq_off.tail)) ||
        acquire(cq_word(params.cq_off.head)) ||
        acquire(cq_word(params.cq_off.tail)))
      return fail(EPROTO);
    return {};
  }

  // Attempts every owned resource once and disables ring operations. Returns
  // the first error, retaining failed mappings so a later reset retries only
  // those unmaps. Initialization stays EBUSY until they are released. Always
  // consumes the fd exactly once, even on EINTR; close is never retried.
  LIBC_INLINE ErrorOr<void> reset() {
    ready = false;
    int error = 0;
    Mapping *mappings[] = {&sqes, &cq, &sq};
    for (Mapping *mapping : mappings) {
      if (mapping->owned) {
        auto result = operations.unmap(mapping->address, mapping->size);
        if (!result) {
          if (!error)
            error = result.error();
          continue;
        }
      }
      *mapping = {};
    }
    if (fd >= 0) {
      auto result = operations.close(fd);
      if (!result && !error)
        error = result.error();
      fd = -1;
    }
    params = {};
    if (error)
      return Error(error);
    return {};
  }

  // Borrowed for completion polling alongside a service control descriptor.
  // Never close it or retain it across reset. Readiness is not CQ consumption.
  LIBC_INLINE int descriptor() const { return ready ? fd : -1; }

  // Capture the CALLING thread's current credentials, not the ring creator's
  // or a later submitter's. Registration is not a request/CQ operation. The
  // service owns the returned ring-local ID, keeps it until all SQEs using it
  // have completed, and serializes release against reuse. Zero selects default
  // credentials in an SQE and must never be returned as a successful capture.
  // Kernel policy can reject later credential override independently.
  LIBC_INLINE ErrorOr<unsigned> register_personality() {
    if (!ready)
      return Error(EBADF);
    auto result = operations.register_operation(
        fd, IORING_REGISTER_PERSONALITY, nullptr, 0);
    if (!result)
      return Error(result.error());
    if (*result <= 0 || unsigned(*result) > UINT16_MAX)
      return Error(EPROTO);
    return static_cast<unsigned>(*result);
  }

  // A failed unregister leaves the service's ownership outstanding for retry
  // or ring teardown. Neither this call nor reset proves I/O has completed.
  LIBC_INLINE ErrorOr<void> unregister_personality(unsigned personality) {
    if (!ready)
      return Error(EBADF);
    if (!personality || personality > UINT16_MAX)
      return Error(EINVAL);
    auto result = operations.register_operation(
        fd, IORING_UNREGISTER_PERSONALITY, nullptr, personality);
    if (!result)
      return Error(result.error());
    if (*result != 0)
      return Error(EPROTO);
    return {};
  }

  // Copies and publishes one standard UAPI SQE, including all 64 user_data
  // bits. Caller initializes reserved fields and owns opcode/resource validity.
  // EAGAIN means full; every error leaves the SQE array and tail unchanged. No
  // syscall.
  LIBC_INLINE ErrorOr<void> enqueue(const io_uring_sqe &entry) {
    auto status = health();
    if (!status)
      return status;
    __u32 tail = acquire(sq_word(params.sq_off.tail));
    __u32 count = tail - acquire(sq_word(params.sq_off.head));
    if (count > params.sq_entries)
      return Error(EPROTO);
    if (count == params.sq_entries)
      return Error(EAGAIN);
    __u32 slot = tail & (params.sq_entries - 1);
    at<io_uring_sqe>(sqes, 0)[slot] = entry;
    at<__u32>(sq, params.sq_off.array)[slot] = slot;
    release(sq_word(params.sq_off.tail), tail + 1);
    return {};
  }

  // Read-only ownership check, separate from syscall failure reporting. An
  // active service can distinguish a normal enter error from lost SQEs/CQEs
  // or corrupted indices without guessing from errno alone. Serialize users
  // of both queues; the kernel may still advance its own head/tail fields.
  LIBC_INLINE ErrorOr<void> check_integrity() const {
    auto status = health();
    if (!status)
      return Error(status.error());
    __u32 sq_tail = acquire(sq_word(params.sq_off.tail));
    __u32 sq_head = acquire(sq_word(params.sq_off.head));
    __u32 cq_head = acquire(cq_word(params.cq_off.head));
    __u32 cq_tail = acquire(cq_word(params.cq_off.tail));
    if (sq_tail - sq_head > params.sq_entries ||
        cq_tail - cq_head > params.cq_entries)
      return Error(EPROTO);
    return {};
  }

  // Requires exclusive access to ALL ring operations, after every enter has
  // returned, with no external submitter or registered BPF loop. Setup rejects
  // SQPOLL and all other flags. In this mode Linux commits SQ head before
  // returning from submission; asynchronous workers only own consumed SQEs.
  // Remove only the last unconsumed entry, identified by its trusted token.
  // No syscall, CQE, or request completion is synthesized. The owner separately
  // retires its completion credit and reports the submission failure. An enter
  // errno alone is never evidence that an entry can be withdrawn.
  // EAGAIN means no unconsumed entry; EINVAL means a different token is last.
  // Every error leaves the queue unchanged, including on corrupt/lost state.
  LIBC_INLINE ErrorOr<void> withdraw_last(uint64_t token) {
    auto status = check_integrity();
    if (!status)
      return status;
    __u32 tail = acquire(sq_word(params.sq_off.tail));
    __u32 head = acquire(sq_word(params.sq_off.head));
    if (tail == head)
      return Error(EAGAIN);
    __u32 slot = (tail - 1) & (params.sq_entries - 1);
    if (at<__u32>(sq, params.sq_off.array)[slot] != slot)
      return Error(EPROTO);
    if (at<io_uring_sqe>(sqes, 0)[slot].user_data != token)
      return Error(EINVAL);
    release(sq_word(params.sq_off.tail), tail - 1);
    return {};
  }

  // Submits the currently published SQ entries with one enter syscall (or
  // returns zero if empty). Returns the exact submitted count, including short
  // success. EINTR/errors are not retried; consult the kernel head on next
  // call, never republish entries merely because enter failed or returned
  // short.
  LIBC_INLINE ErrorOr<unsigned> submit() {
    auto status = health();
    if (!status)
      return Error(status.error());
    __u32 tail = acquire(sq_word(params.sq_off.tail));
    __u32 count = tail - acquire(sq_word(params.sq_off.head));
    if (count > params.sq_entries)
      return Error(EPROTO);
    if (!count)
      return 0U;
    return operations.enter(fd, count, 0, 0);
  }

  // Copies and consumes one CQE; errors leave completion unchanged. EAGAIN
  // means empty. The CQE's signed res is an operation result, not a transport
  // error. If empty with kernel overflow pending, makes at most one nonblocking
  // GETEVENTS call to flush it, preserving syscall errors.
  LIBC_INLINE ErrorOr<void> poll(io_uring_cqe &completion) {
    auto result = take_completion(completion);
    if (result || result.error() != EAGAIN ||
        !(acquire(sq_word(params.sq_off.flags)) & IORING_SQ_CQ_OVERFLOW))
      return result;
    auto flushed = operations.enter(fd, 0, 0, IORING_ENTER_GETEVENTS);
    if (!flushed)
      return Error(flushed.error());
    return take_completion(completion);
  }

  // Consumes an available CQE, otherwise enters once with
  // GETEVENTS/min_complete one and consumes one CQE. Does not submit queued
  // SQEs or retry EINTR; EAGAIN is possible if the syscall succeeds without a
  // visible CQE. No timeout or signal-mask policy is imposed. Errors leave
  // completion unchanged. The caller must have submitted work to wait on.
  LIBC_INLINE ErrorOr<void> wait(io_uring_cqe &completion) {
    auto result = take_completion(completion);
    if (result || result.error() != EAGAIN)
      return result;
    auto waited = operations.enter(fd, 0, 1, IORING_ENTER_GETEVENTS);
    if (!waited)
      return Error(waited.error());
    return take_completion(completion);
  }
};

} // namespace io_uring
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_IO_URING_H
