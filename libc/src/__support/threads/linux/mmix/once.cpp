//===-- MMIX Linux active once initializers -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "once.h"
#include "src/__support/macros/attributes.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
static LIBC_THREAD_LOCAL OnceInitializer *active;
struct alignas(8) Generation {
  cpp::Atomic<uint32_t> value{0};
  uint32_t reserved = 0;
};
static LIBC_CONSTINIT Generation generation;

uint32_t once_fork_generation() { return generation.value.load(); }
bool once_initializer_active() { return active != nullptr; }
void once_after_fork(bool quiescent) {
  // No initializer owner vanished. Retain the epoch, including at its limit.
  if (quiescent)
    return;
  uint32_t next = once_fork_generation() + 0x100;
  if (!next)
    __builtin_trap();
  generation.value.store(next);
  // Foreign flags are repaired lazily. Surviving initializer frames stay owned.
  for (auto *entry = active; entry; entry = entry->previous) {
    uint32_t state = entry->flag->load() & 0xff;
    entry->flag->store(next | state, cpp::MemoryOrder::RELEASE);
  }
}

static void publish_once(CallOnceFlag *flag, uint32_t state) {
  if ((flag->exchange(state, cpp::MemoryOrder::RELEASE) & 0xff) ==
      callonce_impl::WAITING)
    flag->notify_all();
}

OnceInitializer::OnceInitializer(CallOnceFlag *flag, bool owns_completion)
    : flag(flag), previous(active), owns_completion(owns_completion) {
  active = this;
}

OnceInitializer::~OnceInitializer() {
  // Explicit abandonment may already have drained this frame.
  if (active == this)
    active = previous;
  if (owns_completion)
    publish_once(flag, callonce_impl::NOT_CALLED);
}

void OnceInitializer::complete() {
  if (!owns_completion)
    __builtin_trap();
  owns_completion = false;
  publish_once(flag, callonce_impl::FINISH);
}

void abandon_once_initializers() {
  while (active) {
    auto *entry = active;
    active = entry->previous;
    entry->owns_completion = false;
    publish_once(entry->flag, callonce_impl::NOT_CALLED);
  }
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
