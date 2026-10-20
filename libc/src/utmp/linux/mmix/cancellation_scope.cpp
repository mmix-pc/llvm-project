//===-- MMIX accounting cancellation policy -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/utmp/cancellation_scope.h"

#include "hdr/errno_macros.h"
#include "src/__support/threads/linux/mmix/cancellation.h"

#include <pthread.h>

namespace LIBC_NAMESPACE_DECL {
namespace utmp_backend {

CancellationScope::CancellationScope() {
  int error = mmix::set_cancel_state(PTHREAD_CANCEL_DISABLE, &saved);
  // Before thread startup there is no current cancellation context.
  if (error && error != ENOTSUP)
    __builtin_trap();
  restore = !error;
}

CancellationScope::~CancellationScope() noexcept(false) {
  // Re-enabling asynchronous cancellation can unwind here. All accounting
  // locks and temporary descriptors must already have been released.
  if (restore && mmix::set_cancel_state(saved, nullptr))
    __builtin_trap();
}

} // namespace utmp_backend
} // namespace LIBC_NAMESPACE_DECL
