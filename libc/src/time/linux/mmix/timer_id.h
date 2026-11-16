//===-- MMIX default-notification timer identifiers --------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC_TIME_LINUX_MMIX_TIMER_ID_H
#define LLVM_LIBC_SRC_TIME_LINUX_MMIX_TIMER_ID_H

#include "hdr/types/timer_t.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
// Linux's default SIGALRM notification stores its nonnegative int timer ID in
// sival_int. On big-endian LP64 this occupies the high half of sival_ptr. Use
// those same bits for the opaque public default-notification ID. Explicit
// notifications retain their existing low-half IDs. The two encodings cannot
// collide for live kernel IDs; zero has the same representation in both.
LIBC_INLINE timer_t default_notification_timer_id(timer_t kernel) {
  static_assert(sizeof(unsigned long) == 8 && sizeof(int) == 4);
  return reinterpret_cast<timer_t>(reinterpret_cast<unsigned long>(kernel)
                                   << 32);
}

LIBC_INLINE timer_t kernel_timer_id(timer_t timer) {
  unsigned long bits = reinterpret_cast<unsigned long>(timer);
  return reinterpret_cast<timer_t>(bits >> 32 ? bits >> 32 : bits);
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
