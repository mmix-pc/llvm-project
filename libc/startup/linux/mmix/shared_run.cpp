//===-- MMIX Linux dynamic main lifecycle ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "config/linux/app.h"
#include "shared_startup.h"
#include "src/stdlib/exit.h"

namespace LIBC_NAMESPACE_DECL {
namespace {
using Main = int(int, char **, char **);
bool entered;

void run_main(void *argument) {
  Main *entry = *static_cast<Main **>(argument);
  exit(entry(static_cast<int>(app.args->argc),
             reinterpret_cast<char **>(app.args->argv),
             reinterpret_cast<char **>(app.env_ptr)));
}
} // namespace
} // namespace LIBC_NAMESPACE_DECL

// Private cross-image CRT hook; provider-local references bind locally.
extern "C" [[noreturn, gnu::visibility("protected")]] void
__llvm_libc_mmix_dynamic_start(int (*entry)(int, char **, char **),
                               const void *sp) {
  using namespace LIBC_NAMESPACE;
  if (entered || !entry || !app.args || sp != app.args ||
      mmix::current_control != &mmix::shared_main ||
      reinterpret_cast<uintptr_t>(__builtin_thread_pointer()) !=
          mmix::shared_main.tls.tp)
    __builtin_trap();
  entered = true;
  // Loader adoption and all startup callbacks have already completed.
  mmix::run_with_unwind_root(mmix::shared_main, run_main, &entry);
  __builtin_trap();
}
