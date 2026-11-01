//===-- MMIX managed notification callback --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "notification_callback.h"
#include "hdr/errno_macros.h"
#include "hdr/pthread_macros.h"
#include "hdr/signal_macros.h"
#include "src/__support/CPP/new.h"
#include "src/stdlib/free.h"
#include "src/stdlib/malloc.h"
#ifdef LIBC_MMIX_SHARED_STARTUP
#include "src/__support/OSUtil/linux/mmix/dynamic_tls.h"
#endif

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

long NotificationCallback::create(const sigevent &event, const TLSImage &image,
                                  uintptr_t page_size,
                                  NotificationCallback *&output) {
  if (event.sigev_notify != SIGEV_THREAD || !event.sigev_notify_function ||
      page_size != 8192)
    return EINVAL;
  ThreadPreparation request;
  request.image = image;
  request.page_size = page_size;
  request.detached = true;
  if (event.sigev_notify_attributes) {
    const auto attr = *event.sigev_notify_attributes;
    if ((attr.__detachstate != PTHREAD_CREATE_JOINABLE &&
         attr.__detachstate != PTHREAD_CREATE_DETACHED) ||
        attr.__stacksize < PTHREAD_STACK_MIN || attr.__guardsize % page_size ||
        (attr.__inheritsched != PTHREAD_INHERIT_SCHED &&
         attr.__inheritsched != PTHREAD_EXPLICIT_SCHED))
      return EINVAL;
    if (attr.__scope != PTHREAD_SCOPE_SYSTEM)
      return attr.__scope == PTHREAD_SCOPE_PROCESS ? ENOTSUP : EINVAL;
    request.stack = attr.__stack;
    request.stack_size = attr.__stacksize;
    request.guard_size = attr.__guardsize;
    if (attr.__inheritsched == PTHREAD_EXPLICIT_SCHED) {
#ifdef LIBC_MMIX_PTHREAD_SCHEDULING
      if (int error =
              scheduling_validate(attr.__schedpolicy, attr.__schedpriority))
        return error;
      request.apply_scheduling = true;
      request.scheduling_policy = attr.__schedpolicy;
      request.scheduling_priority = attr.__schedpriority;
#else
      return ENOTSUP;
#endif
    }
  }
  constexpr uintptr_t maximum = UINTPTR_MAX >> 1;
  if (request.stack) {
    uintptr_t bottom = reinterpret_cast<uintptr_t>(request.stack);
    if (bottom > maximum || request.stack_size > maximum - bottom)
      return EOVERFLOW;
    if (bottom % 8 || (bottom + request.stack_size) % 8)
      return EINVAL;
  } else {
    size_t stack_padding =
        (page_size - request.stack_size % page_size) % page_size;
    if (request.stack_size > maximum ||
        stack_padding > maximum - request.stack_size ||
        request.guard_size > maximum - request.stack_size - stack_padding)
      return EOVERFLOW;
  }

  void *storage = LIBC_NAMESPACE::malloc(sizeof(NotificationCallback));
  if (!storage)
    return ENOMEM;
  auto *callback = new (storage) NotificationCallback;
#ifdef LIBC_MMIX_SHARED_STARTUP
  long error = retain_shared_tls_destructor(
      nullptr, reinterpret_cast<uintptr_t>(event.sigev_notify_function),
      &callback->image_pin);
  if (error) {
    callback->~NotificationCallback();
    LIBC_NAMESPACE::free(storage);
    return error;
  }
#endif
  callback->function = event.sigev_notify_function;
  callback->value = event.sigev_value;
  request.runner.posix_runner = invoke;
  request.argument = callback;
  request.resource_owner = {callback, retain_owner, release_owner, invoke};
  callback->preparation = request;
  output = callback;
  return 0;
}

long NotificationCallback::retain_owner(void *pointer) {
  auto &callback = *static_cast<NotificationCallback *>(pointer);
  auto count = callback.references.load(cpp::MemoryOrder::ACQUIRE);
  for (;;) {
    if (!count || count == UINT64_MAX)
      return EOVERFLOW;
    if (callback.references.compare_exchange_weak(count, count + 1,
                                                  cpp::MemoryOrder::ACQ_REL))
      return 0;
  }
}

long NotificationCallback::release_owner(void *pointer) {
  auto *callback = static_cast<NotificationCallback *>(pointer);
  auto count = callback->references.load(cpp::MemoryOrder::ACQUIRE);
  for (;;) {
    if (!count)
      return EINVAL;
    if (callback->references.compare_exchange_weak(count, count - 1,
                                                   cpp::MemoryOrder::ACQ_REL))
      break;
  }
  if (count != 1)
    return 0;
  // The final reference is exclusive. Loader release only drops Runtime
  // ownership; it does not invoke finalizers. Failure leaves a retryable owner.
#ifdef LIBC_MMIX_SHARED_STARTUP
  if (long error = release_shared_tls_destructor(callback->image_pin)) {
    callback->references.store(1, cpp::MemoryOrder::RELEASE);
    return error;
  }
#endif
  callback->~NotificationCallback();
  LIBC_NAMESPACE::free(callback);
  return 0;
}

void *NotificationCallback::invoke(void *pointer) {
  auto &callback = *static_cast<NotificationCallback *>(pointer);
  callback.function(callback.value);
  // Ownership belongs to thread resources, including when user code exits or
  // is cancelled and never returns here.
  return nullptr;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
