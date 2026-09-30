//===-- MMIX Linux child startup ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "thread_start.h"
#include "hdr/errno_macros.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/lifecycle.h"
#include "src/__support/threads/thread.h"
#include <linux/futex.h>
#include <sys/syscall.h>

#if LIBC_THREAD_MODE != LIBC_THREAD_MODE_PLATFORM ||                           \
    LIBC_ERRNO_MODE != LIBC_ERRNO_MODE_THREAD_LOCAL
#error "MMIX Linux child startup requires platform TLS and TLS errno"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

static int initialize_child(ThreadControl &control) {
  if (internal::self.attrib)
    __builtin_trap();
  libc_errno = 0;
  long tid = syscall_impl(SYS_gettid);
  if (tid <= 0 || tid > cpp::numeric_limits<int>::max())
    return tid < 0 && tid >= -4095 ? static_cast<int>(-tid) : EINVAL;

  auto &attributes = control.attributes;
  attributes.tid = static_cast<int>(tid);
  attributes.stack = reinterpret_cast<void *>(control.stack_bottom);
  attributes.stacksize = control.stack_top - control.stack_bottom;
  attributes.guardsize = control.guard_size;
  attributes.owned_stack = control.owns_stack;
  attributes.tls = control.tls.addr;
  attributes.tls_size = control.tls.size;
  attributes.platform_data = &control.clear_tid.value.val;
  internal::self.attrib = &attributes;
  attributes.atexit_callback_mgr = internal::get_thread_atexit_callback_mgr();
  if (!attributes.atexit_callback_mgr)
    return ENOMEM;
  current_control = &control;
  return 0;
}

extern "C" [[noreturn, clang::noinline, clang::disable_tail_calls]] void
__llvm_libc_mmix_thread_start(ThreadControl *control) {
  // The kernel already installed TP and a fresh root. PREPARED must precede
  // clone; no parent frame, user initializer or signal-mask restoration here.
  if (!control || !control->is_prepared())
    __builtin_trap();
  int error = initialize_child(*control);
  control->publish_child(error ? ChildStatus::Failed : ChildStatus::Ready,
                         error);
  long wake = syscall_impl(
      SYS_futex, reinterpret_cast<long>(&control->child_status.value.val),
      FUTEX_WAKE_PRIVATE, cpp::numeric_limits<int>::max(), 0, 0, 0);
  if (wake < 0)
    __builtin_trap();

  for (;;) {
    CreatorDecision decision = control->acquire_decision();
    if (decision == CreatorDecision::Go) {
      if (error)
        __builtin_trap();
      __llvm_libc_mmix_thread_run(control);
      __builtin_trap();
    }
    if (decision == CreatorDecision::Abort) {
      __llvm_libc_mmix_thread_abort(control);
      __builtin_trap();
    }
    if (decision != CreatorDecision::Hold)
      __builtin_trap();
    long ret = syscall_impl(
        SYS_futex, reinterpret_cast<long>(&control->creator_decision.value.val),
        FUTEX_WAIT_BITSET_PRIVATE, static_cast<uint32_t>(CreatorDecision::Hold),
        0, 0, FUTEX_BITSET_MATCH_ANY);
    if (ret < 0 && ret != -EINTR && ret != -EAGAIN)
      __builtin_trap();
  }
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
