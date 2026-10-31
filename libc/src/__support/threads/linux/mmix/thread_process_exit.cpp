//===-- MMIX Linux last application thread exit ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "main_thread.h"
#include "src/stdlib/linux/mmix/process_exit.h"
#include "thread_finish.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
#ifdef LIBC_MMIX_UNWIND_ROOTS
static void finalize_last_thread(void *) {
  internal::exit_after_thread_cleanup();
}
#endif
[[noreturn]] void terminate_after_thread_cleanup() {
#ifdef LIBC_MMIX_UNWIND_ROOTS
  // Only the initial thread (and its copied descendants) gets this root.
  // Fork still requires the locked joinable/unqueued, sole-task certificate;
  // merely entering finalization does not grant a detached caller recovery.
  if (current_control && current_control->terminal_cleanup_finished() &&
      internal::main_thread_cleanup_finished())
    run_with_process_exit_root(*current_control, finalize_last_thread, nullptr);
#endif
  // Reuse normal exit's process callbacks and terminal transport, but do not
  // replay thread cleanup or run main's TLS callbacks on the last worker.
  internal::exit_process(0);
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
