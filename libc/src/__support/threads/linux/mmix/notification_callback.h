//===-- MMIX managed notification callback ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_NOTIFICATION_CALLBACK_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_NOTIFICATION_CALLBACK_H

#include "hdr/types/struct_sigevent.h"
#include "resources.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

// Immutable callback/attribute snapshot with independent registration and
// thread-resource references. Callers suppress cancellation and hold a live
// reference through every operation. Never release under a registry lock.
class NotificationCallback {
  cpp::Atomic<uint64_t> references{1};
  ThreadPreparation preparation;
  void (*function)(sigval) = nullptr;
  sigval value{};
#ifdef LIBC_MMIX_SHARED_STARTUP
  uint64_t image_pin = 0;
#endif

  NotificationCallback() = default;
  static long retain_owner(void *);
  static long release_owner(void *);
  static void *invoke(void *);

public:
  NotificationCallback(const NotificationCallback &) = delete;
  NotificationCallback &operator=(const NotificationCallback &) = delete;

  // Positive errno; failure leaves output unchanged and owns no resources.
  static long create(const sigevent &, const TLSImage &, uintptr_t page_size,
                     NotificationCallback *&output);
  ThreadPreparation thread_preparation() const { return preparation; }
  bool supplied_stack() const { return preparation.stack != nullptr; }
  long retain() { return retain_owner(this); }
  // Failure preserves this reference for retry; success may destroy this.
  long release() { return release_owner(this); }
};

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
