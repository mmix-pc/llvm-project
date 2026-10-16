//===-- MMIX Linux fork callback phases -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "atfork_callbacks.h"
#include "src/__support/threads/raw_mutex.h"
#ifdef LIBC_MMIX_SHARED_STARTUP
#include "callback_ownership.h"
#endif
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {
struct Triple {
  ForkCallback *prepare = nullptr, *parent = nullptr, *child = nullptr;
#ifdef LIBC_MMIX_SHARED_STARTUP
  uint64_t pins[3] = {};
#endif
};
struct Registry {
  alignas(8) RawMutex mutex;
  Triple callbacks[32];
  size_t count = 0;
};
static LIBC_CONSTINIT Registry registry;
} // namespace
bool register_fork_callbacks(ForkCallback *prepare, ForkCallback *parent,
                             ForkCallback *child) {
  Triple pending{prepare, parent, child};
#ifdef LIBC_MMIX_SHARED_STARTUP
  CallbackOwnershipScope scope;
  ForkCallback *callbacks[] = {prepare, parent, child};
  for (unsigned i = 0; i != 3; ++i) {
    if (callbacks[i] && retain_shared_tls_destructor(
                            nullptr, reinterpret_cast<uintptr_t>(callbacks[i]),
                            &pending.pins[i])) {
      while (i)
        if (auto pin = pending.pins[--i])
          if (release_shared_tls_destructor(pin))
            __builtin_trap();
      return false;
    }
  }
#endif
  registry.mutex.lock();
  bool accepted = registry.count != 32;
  if (accepted)
    registry.callbacks[registry.count++] = pending;
  registry.mutex.unlock();
#ifdef LIBC_MMIX_SHARED_STARTUP
  // No unregister API: accepted registrations intentionally retain their code
  // for the process lifetime, including every acquired fork-phase snapshot.
  if (!accepted)
    for (auto pin : pending.pins)
      if (pin && release_shared_tls_destructor(pin))
        __builtin_trap();
#endif
  return accepted;
}
size_t prepare_fork_callbacks() {
  registry.mutex.lock();
  size_t count = registry.count;
  registry.mutex.unlock();
  // Published entries are immutable. Later registrations belong to later forks.
  for (size_t i = count; i; --i)
    if (auto *callback = registry.callbacks[i - 1].prepare)
      callback();
  return count;
}
void lock_fork_callbacks() { registry.mutex.lock(); }
void finish_fork_callbacks(size_t count, bool child) {
  registry.mutex.unlock();
  for (size_t i = 0; i < count; ++i) {
    auto *callback =
        child ? registry.callbacks[i].child : registry.callbacks[i].parent;
    if (callback)
      callback();
  }
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
