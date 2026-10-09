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
bool once_can_fork() { return once_fork_generation() != 0xffffff00U; }
void once_after_fork() {
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

OnceInitializer::OnceInitializer(CallOnceFlag *flag)
    : flag(flag), previous(active) {
  active = this;
}

OnceInitializer::~OnceInitializer() { active = previous; }

void abandon_once_initializers() {
  while (active) {
    auto *entry = active;
    active = entry->previous;
    callonce_impl::abandon(entry->flag);
  }
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
