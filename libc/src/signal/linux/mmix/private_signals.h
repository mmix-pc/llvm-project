//===-- MMIX Linux private signal policy ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC_SIGNAL_LINUX_MMIX_PRIVATE_SIGNALS_H
#define LLVM_LIBC_SRC_SIGNAL_LINUX_MMIX_PRIVATE_SIGNALS_H
#include "src/__support/macros/attributes.h"
#include "src/__support/macros/config.h"
#include <stdint.h>
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
inline constexpr int NOTIFICATION_SIGNAL = 34;
inline constexpr uint64_t notification_signal_mask =
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
    uint64_t(1) << (NOTIFICATION_SIGNAL - 1);
#else
    0;
#endif
inline constexpr uint64_t private_signal_mask =
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
    notification_signal_mask | (uint64_t(1) << 32) |
#endif
#ifdef LIBC_MMIX_CANCELLATION_WINDOWS
    (uint64_t(1) << 31) |
#endif
    0;
inline bool private_signal(int number) {
  return number > 0 && number <= 64 &&
         (private_signal_mask & (uint64_t(1) << (number - 1)));
}
// Unlike credentials, the notification signal may be privately blocked for a
// synchronous dispatcher. Public replacement masks must retain that state.
// This reservation does not install a handler or provide timer dispatch.
inline uint64_t public_replacement_mask(uint64_t requested, uint64_t previous) {
  return (requested & ~private_signal_mask) |
         (previous & notification_signal_mask);
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
