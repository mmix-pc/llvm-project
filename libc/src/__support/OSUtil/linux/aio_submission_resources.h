//===-- Private Linux AIO submission resources ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_AIO_SUBMISSION_RESOURCES_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_AIO_SUBMISSION_RESOURCES_H

#include "hdr/errno_macros.h"
#include "hdr/fcntl_macros.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace aio_submission_resources {

// Checked values or positive errno, without libc_errno or public cancellation
// wrappers. Each call makes one syscall. Successful duplicate transfers a new,
// nonnegative CLOEXEC fd; close consumes it even on error, including EINTR.
// Injected Operations must obey the same contracts. Their backing state must
// outlive the owner, which stores Operations by value.
struct LinuxOperations {
  LIBC_INLINE ErrorOr<int> duplicate(int fd) {
#ifdef SYS_fcntl
    constexpr long NUMBER = SYS_fcntl;
#else
    constexpr long NUMBER = SYS_fcntl64;
#endif
    // This command has no offset/lock structure, including on 32-bit Linux.
    return linux_syscalls::syscall_checked<int>(NUMBER, fd, F_DUPFD_CLOEXEC, 0);
  }

  LIBC_INLINE ErrorOr<int> close(int fd) {
    return linux_syscalls::syscall_checked<int>(SYS_close, fd);
  }
};

} // namespace aio_submission_resources

// Caller-serialized capture ownership, not a public AIO provider. Serialize
// every operation/query and suppress cancellation and application signals
// throughout capture/release and the surrounding admission transaction.
// capture MUST run on the submitting application thread: moving registration
// to a service thread would capture that thread's credentials instead.
//
// RingLike supplies io_uring::Ring's register_personality() and
// unregister_personality(unsigned) contracts: successful registration returns
// a nonzero 16-bit ID; failed unregister retains it. The bound ring must
// outlive this stable owner and MUST NOT reset while it owns a personality,
// including failed admission rollback. The service also serializes ring
// lifetime/reuse.
//
// The service holds these resources through ALL metadata, transfer and sync
// phases, including gaps between SQEs. release requires its guarantee that no
// dependent SQE can still use either resource and no future one will use them.
// Neither aio_return nor a cancel CQE proves this. This helper deliberately
// duplicates no request counters and cannot infer terminal completion.
template <class RingLike,
          class Operations = aio_submission_resources::LinuxOperations>
class AioSubmissionResources {
  RingLike &ring;
  Operations operations;
  int fd = -1;
  unsigned personality_id = 0;
  bool captured = false;
  int admission_error = 0;
  int release_error = 0;

  LIBC_INLINE ErrorOr<void> fail(int error) {
    admission_error = error;
    (void)release();
    return Error(error);
  }

public:
  LIBC_INLINE explicit AioSubmissionResources(RingLike &ring,
                                              Operations operations = {})
      : ring(ring), operations(operations) {}
  AioSubmissionResources(const AioSubmissionResources &) = delete;
  AioSubmissionResources &operator=(const AioSubmissionResources &) = delete;
  AioSubmissionResources(AioSubmissionResources &&) = delete;
  AioSubmissionResources &operator=(AioSubmissionResources &&) = delete;

  // No implicit cleanup: it could release live SQE dependencies or hide a
  // failed unregister. The service must explicitly drain, even after failure.
  LIBC_INLINE ~AioSubmissionResources() {
    if (!drained())
      __builtin_trap();
  }

  // Register caller credentials, then hold the open file description with an
  // atomic CLOEXEC duplicate. Only full success permits admission. The source
  // fd is borrowed for this call; the caller excludes its close/reuse
  // meanwhile. On failure return/record the primary error, attempt rollback
  // once, and inspect drained()/cleanup_error() for remaining cleanup
  // responsibility. EBUSY changes nothing, including the recorded errors; retry
  // capture only after draining. A new admissible attempt clears the previous
  // diagnostics.
  LIBC_INLINE ErrorOr<void> capture(int source_fd) {
    if (!drained())
      return Error(EBUSY);
    admission_error = release_error = 0;
    auto personality = ring.register_personality();
    if (!personality) {
      admission_error = personality.error();
      return Error(admission_error);
    }
    personality_id = *personality;
    auto duplicate = operations.duplicate(source_fd);
    if (!duplicate)
      return fail(duplicate.error());
    if (*duplicate < 0)
      return fail(EPROTO);
    fd = *duplicate;
    captured = true;
    return {};
  }

  // Under the caller's terminal/no-future-use guarantee above, disable access
  // and release in reverse acquisition order. Attempt both resources once,
  // returning the first cleanup error, never overwriting the admission error.
  // close consumes the fd even on failure: retrying could close a reused fd
  // number. A failed unregister keeps its ID for a later explicit release.
  // drained() may be true even when release reports a close error.
  LIBC_INLINE ErrorOr<void> release() {
    captured = false;
    release_error = 0;
    if (fd >= 0) {
      int closing = fd;
      fd = -1;
      auto result = operations.close(closing);
      if (!result)
        release_error = result.error();
    }
    if (personality_id) {
      auto result = ring.unregister_personality(personality_id);
      if (result)
        personality_id = 0;
      else if (!release_error)
        release_error = result.error();
    }
    if (release_error)
      return Error(release_error);
    return {};
  }

  LIBC_INLINE bool ready() const { return captured; }
  LIBC_INLINE bool drained() const { return fd < 0 && personality_id == 0; }
  LIBC_INLINE int error() const { return admission_error; }
  LIBC_INLINE int cleanup_error() const { return release_error; }

  // Borrowed only while ready, never ownership transfer. Hide partial capture
  // or release state so callers cannot publish SQEs after a failed admission.
  LIBC_INLINE int descriptor() const { return captured ? fd : -1; }
  LIBC_INLINE unsigned personality() const {
    return captured ? personality_id : 0;
  }
};

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_AIO_SUBMISSION_RESOURCES_H
