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
