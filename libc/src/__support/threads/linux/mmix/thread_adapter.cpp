//===-- MMIX Linux thread adapter -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "config/app.h"
#include "hdr/signal_macros.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "src/__support/threads/linux/mmix/thread_create.h"
#include "src/__support/threads/linux/mmix/thread_finish.h"
#include "src/__support/threads/linux/mmix/thread_join.h"
#include "src/__support/threads/thread.h"
#include <sys/syscall.h>

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

int Thread::join(ThreadReturnValue &retval) {
  uint64_t blocked = UINT64_MAX, saved = 0;
  if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                   reinterpret_cast<long>(&blocked),
                   reinterpret_cast<long>(&saved), sizeof(saved)) != 0)
    __builtin_trap();
  int error = mmix::join_thread(attrib, retval);
  if (syscall_impl(SYS_rt_sigprocmask, SIG_SETMASK,
                   reinterpret_cast<long>(&saved), 0, sizeof(saved)) != 0)
    __builtin_trap();
  return error;
}

[[noreturn]] void thread_exit(ThreadReturnValue retval, ThreadStyle style) {
  auto *control = mmix::current_control;
  if (!control || internal::self.attrib != &control->attributes ||
      (style != ThreadStyle::POSIX && style != ThreadStyle::STDC))
    __builtin_trap();
  // FIXME: Add forced unwinding when C++ pthread_exit cleanup is admitted.
  mmix::exit_thread(*control, retval);
}

} // namespace LIBC_NAMESPACE_DECL
