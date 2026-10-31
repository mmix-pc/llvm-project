//===-- MMIX AIO public input snapshots ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_AIO_INPUT_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_AIO_INPUT_H

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "hdr/limits_macros.h"
#include "hdr/signal_macros.h"
#include "hdr/stdint_proxy.h"
#include "src/__support/CPP/new.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"
#include <aio.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// Only the discriminator and fields meaningful for that discriminator are
// read. Unknown modes survive for synchronous notification preparation to
// reject. THREAD attributes remain BORROWED: prepare must copy/validate them
// synchronously before acceptance. This snapshot is not a lifetime proof for
// attributes, value pointees, buffers, or the application's control block.
LIBC_INLINE sigevent snapshot_aio_event(const sigevent *event) {
  sigevent result{};
  result.sigev_notify = event ? event->sigev_notify : SIGEV_NONE;
  if (result.sigev_notify == SIGEV_SIGNAL) {
    result.sigev_signo = event->sigev_signo;
    result.sigev_value = event->sigev_value;
  } else if (result.sigev_notify == SIGEV_THREAD) {
    result.sigev_value = event->sigev_value;
    result.sigev_notify_function = event->sigev_notify_function;
    result.sigev_notify_attributes = event->sigev_notify_attributes;
  }
  return result;
}

// Public-width input, deliberately NOT AioEngine::Input. A provider must still
// validate/normalize the transfer before converting size_t/off_t to the engine
// representation. No size cap, offset rule, or seekability policy lives here.
struct AioInput {
  const aiocb *control_block = nullptr;
  int opcode = LIO_NOP;
  int member_error = 0;
  int descriptor = -1;
  volatile void *buffer = nullptr;
  size_t nbytes = 0;
  off_t offset = 0;
  sigevent event = snapshot_aio_event(nullptr);
};

// Linux import_ubuf further caps this at MAX_RW_COUNT (INT_MAX & PAGE_MASK).
// Saturating here preserves that single-transfer result without embedding a
// page size or narrowing a public size_t before checking its permitted range.
// The caller must report the actual CQE count, not the original request size.
LIBC_INLINE int normalize_aio_transfer_size(size_t nbytes, uint32_t &size) {
  if (nbytes > size_t(SSIZE_MAX))
    return EINVAL;
  size = nbytes > size_t(INT32_MAX) ? uint32_t(INT32_MAX) : uint32_t(nbytes);
  return 0;
}

struct AioSyncInput {
  const aiocb *control_block = nullptr;
  int descriptor = -1;
  bool data_only = false;
  sigevent event = snapshot_aio_event(nullptr);
};

// aio_fsync ignores every control-block field except descriptor and event.
// Invalid operation selection fails before dereferencing the control block;
// a failed capture leaves the caller's previously captured output untouched.
LIBC_INLINE int snapshot_aio_sync_input(int operation, const aiocb *cb,
                                        AioSyncInput &output) {
  if ((operation != O_SYNC && operation != O_DSYNC) || !cb)
    return EINVAL;
  AioSyncInput result;
  result.control_block = cb;
  result.descriptor = cb->aio_fildes;
  result.data_only = operation == O_DSYNC;
  result.event = snapshot_aio_event(&cb->aio_sigevent);
  output = result;
  return 0;
}

// A direct aio_read/aio_write wrapper supplies LIO_READ/LIO_WRITE, ignoring
// aio_lio_opcode. List capture supplies the public opcode. NULL/NOP are absent
// members; a malformed opcode retains only the key, opcode and member EINVAL.
// aio_reqprio is deliberately ignored, and __request is never read or written.
LIBC_INLINE AioInput snapshot_aio_input(const aiocb *cb, int opcode) {
  AioInput result;
  if (!cb || opcode == LIO_NOP)
    return result;
  result.control_block = cb;
  result.opcode = opcode;
  if (opcode != LIO_READ && opcode != LIO_WRITE) {
    result.member_error = EINVAL;
    return result;
  }
  result.descriptor = cb->aio_fildes;
  result.buffer = cb->aio_buf;
  result.nbytes = cb->aio_nbytes;
  result.offset = cb->aio_offset;
  result.event = snapshot_aio_event(&cb->aio_sigevent);
  return result;
}

// A wrapper's private, pre-acceptance scratch storage, not a service owner.
// The application must synchronize the list, control blocks and event inputs
// throughout capture and synchronous preparation. Multiple passes do not make
// concurrent application mutation safe. Active service bindings must still be
// checked at admission under the service gate.
//
// Allocator follows AioStorage: nonthrowing allocate(size, alignment),
// returning aligned storage or nullptr, and deallocate(ptr). It must outlive
// this object. Capture/destruction run outside service gates, with
// wrapper-owned cleanup on cancellation/jump paths. Destroy this scratch object
// before a public wait that could abandon its stack; never rely on RAII across
// application longjmp.
template <class Allocator> class AioListInput {
  Allocator &allocator;
  AioInput *members = nullptr;
  size_t count = 0;
  int list_mode = LIO_WAIT;
  sigevent event = snapshot_aio_event(nullptr);
  bool captured = false;

public:
  LIBC_INLINE explicit AioListInput(Allocator &allocator)
      : allocator(allocator) {}
  AioListInput(const AioListInput &) = delete;
  AioListInput &operator=(const AioListInput &) = delete;
  LIBC_INLINE ~AioListInput() {
    if (members) {
      for (size_t i = 0; i < count; ++i)
        members[i].~AioInput();
      allocator.deallocate(members);
    }
  }

  // Positive internal errno, never public errno/aggregate mapping. No member
  // is accepted here. Failure leaves the object empty and retryable; success
  // is single-use. Zero entries is valid, including a null list pointer.
  LIBC_INLINE int capture(int mode, aiocb *const *list, int nent,
                          const sigevent *sig) {
    if (captured)
      return EBUSY;
    if ((mode != LIO_WAIT && mode != LIO_NOWAIT) || nent < 0 || (nent && !list))
      return EINVAL;

    // Validate the whole list using only keys and opcodes, before allocation
    // or I/O/event field reads. Duplicated ignored entries are harmless.
    size_t real_count = 0;
    for (int i = 0; i < nent; ++i) {
      const aiocb *cb = list[i];
      if (!cb || cb->aio_lio_opcode == LIO_NOP)
        continue;
      for (int j = 0; j < i; ++j)
        if (list[j] == cb)
          return EINVAL;
      ++real_count;
    }
    if (real_count > SIZE_MAX / sizeof(AioInput))
      return ENOMEM;
    if (real_count) {
      void *memory =
          allocator.allocate(real_count * sizeof(AioInput), alignof(AioInput));
      if (!memory)
        return ENOMEM;
      members = static_cast<AioInput *>(memory);
      for (int i = 0; i < nent; ++i) {
        const aiocb *cb = list[i];
        if (!cb || cb->aio_lio_opcode == LIO_NOP)
          continue;
        new (&members[count++])
            AioInput(snapshot_aio_input(cb, cb->aio_lio_opcode));
      }
    }
    list_mode = mode;
    // In WAIT even the supplied pointer is ignored, not just its contents.
    event = snapshot_aio_event(mode == LIO_NOWAIT ? sig : nullptr);
    captured = true;
    return 0;
  }

  LIBC_INLINE bool ready() const { return captured; }
  LIBC_INLINE size_t size() const { return count; }
  LIBC_INLINE const AioInput *data() const { return members; }
  LIBC_INLINE int mode() const { return list_mode; }
  LIBC_INLINE const sigevent &notification() const { return event; }
};

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_AIO_INPUT_H
