//===-- MMIX Linux thread adapter -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "config/app.h"
#include "src/__support/threads/linux/mmix/thread_create.h"
#include "src/__support/threads/thread.h"

namespace LIBC_NAMESPACE_DECL {

int Thread::run(ThreadStyle style, ThreadRunner runner, void *arg, void *stack,
                size_t stacksize, size_t guardsize, bool detached) {
  mmix::ThreadPreparation request;
  request.style = style;
  request.runner = runner;
  request.argument = arg;
  request.stack = stack;
  request.stack_size = stacksize;
  request.guard_size = guardsize;
  request.detached = detached;
  request.image = app.tls;
  request.page_size = app.page_size;

  ThreadAttributes *created = nullptr;
  mmix::ThreadCreationResult result = mmix::create_thread(request, created);
  // A public caller cannot assume ownership of a partially rolled-back mapping.
  if (result.preparation.retained.control.size ||
      result.preparation.retained.stack.size ||
      result.preparation.retained.tls.size)
    __builtin_trap();
  if (result.error)
    return static_cast<int>(-result.error);
  if (!created)
    __builtin_trap();
  attrib = created;
  return 0;
}

bool Thread::operator==(const Thread &other) const {
  return attrib == other.attrib;
}

} // namespace LIBC_NAMESPACE_DECL
