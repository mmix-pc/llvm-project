//===-- MMIX Linux last application thread exit ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "main_thread.h"
#include "thread_finish.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
[[noreturn]] void terminate_after_thread_cleanup() {
  // Reuse normal exit's process callbacks and terminal transport, but do not
  // replay thread cleanup or run main's TLS callbacks on the last worker.
  internal::exit_process(0);
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
