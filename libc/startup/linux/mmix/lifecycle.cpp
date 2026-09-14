//===-- MMIX Linux initialization lifecycle -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "config/linux/app.h"
#include "hdr/types/size_t.h"
#include "src/__support/threads/linux/mmix/main_thread.h"
#include "src/stdlib/atexit.h"
#include "src/stdlib/exit.h"

extern "C" {
int main(int argc, char **argv, char **envp);
extern uintptr_t __preinit_array_start[], __preinit_array_end[];
extern uintptr_t __init_array_start[], __init_array_end[];
extern uintptr_t __fini_array_start[], __fini_array_end[];
[[noreturn, gnu::visibility("hidden")]] void
__llvm_libc_mmix_linux_start_fail();
}

namespace LIBC_NAMESPACE_DECL {
namespace {
using InitCallback = void(int, char **, char **);
using FiniCallback = void();

// Preserve the common Linux startup's forward init and reverse fini traversal.
void call_init_array_callbacks(int argc, char **argv, char **env) {
  size_t count = __preinit_array_end - __preinit_array_start;
  for (size_t i = 0; i < count; ++i)
    reinterpret_cast<InitCallback *>(__preinit_array_start[i])(argc, argv, env);
  count = __init_array_end - __init_array_start;
  for (size_t i = 0; i < count; ++i)
    reinterpret_cast<InitCallback *>(__init_array_start[i])(argc, argv, env);
}

void call_fini_array_callbacks() {
  size_t count = __fini_array_end - __fini_array_start;
  for (size_t i = count; i > 0; --i)
    reinterpret_cast<FiniCallback *>(__fini_array_start[i - 1])();
}
} // namespace
} // namespace LIBC_NAMESPACE_DECL

extern "C" [[noreturn, gnu::visibility("hidden")]] void
__llvm_libc_mmix_linux_run() {
  using namespace LIBC_NAMESPACE;
  if (!internal::initialize_main_thread())
    __llvm_libc_mmix_linux_start_fail();
  // Register before constructors so their exit callbacks run before fini.
  if (atexit(&call_fini_array_callbacks) != 0)
    __llvm_libc_mmix_linux_start_fail();
  int argc = static_cast<int>(app.args->argc);
  auto **argv = reinterpret_cast<char **>(app.args->argv);
  auto **env = reinterpret_cast<char **>(app.env_ptr);
  call_init_array_callbacks(argc, argv, env);
  exit(main(argc, argv, env));
}
