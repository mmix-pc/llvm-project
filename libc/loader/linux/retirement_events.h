//===-- Deferred loader retirement notifications -----------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_LOADER_LINUX_RETIREMENT_EVENTS_H
#define LLVM_LIBC_LOADER_LINUX_RETIREMENT_EVENTS_H

#include "src/__support/CPP/atomic.h"

namespace LIBC_NAMESPACE_DECL {
namespace loader {

// Install before publishing producers; outlive every ticket and reader. Wake
// must only enqueue a safe point, never run callbacks or take loader locks.
// Signal and unwind release paths require an allocation-free, signal-safe wake.
class RetirementEvents {
  cpp::Atomic<bool> pending{false};
  void *context;
  void (*wake)(void *);

public:
  RetirementEvents(void *context, void (*wake)(void *))
      : context(context), wake(wake) {
    if (!wake)
      __builtin_trap();
  }
  void request() {
    if (!pending.exchange(true))
      wake(context);
  }
  bool take() { return pending.exchange(false); }
  bool requested() { return pending.load(); }
};

} // namespace loader
} // namespace LIBC_NAMESPACE_DECL
#endif
