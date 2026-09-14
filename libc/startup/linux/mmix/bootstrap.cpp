//===-- MMIX Linux bootstrap state ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "process_args.h"
#include "src/errno/program_invocation_name.h"
#include "src/errno/program_invocation_short_name.h"
#include "src/unistd/environ.h"

// These providers complete the bootstrap in the guard and lifecycle objects.
extern "C" {
[[noreturn, gnu::visibility("hidden")]] void
__llvm_libc_mmix_linux_start_fail();
[[gnu::visibility("hidden")]] void
__llvm_libc_mmix_linux_init_guard(const unsigned char *random);
[[noreturn, gnu::visibility("hidden")]] void __llvm_libc_mmix_linux_run();
}

namespace LIBC_NAMESPACE_DECL {
AppProperties app;

void mmix::publish_process_args(const ProcessArgs &args) {
  app.args = args.args;
  app.env_ptr = args.env;
  app.page_size = args.page_size;
  auxv::Vector::initialize_unsafe(args.aux);
  environ = reinterpret_cast<char **>(args.env);
  if (args.args->argc != 0) {
    program_invocation_name = reinterpret_cast<char *>(args.args->argv[0]);
    program_invocation_short_name = program_invocation_name;
    for (char *p = program_invocation_name; *p != '\0'; ++p)
      if (*p == '/')
        program_invocation_short_name = p + 1;
  }
}
} // namespace LIBC_NAMESPACE_DECL

// FIXME: Replace TLS-free single-thread startup when MMIX Linux gains
// pthread/TLS and dynamic-loader integration.
extern "C" [[noreturn, gnu::visibility("hidden")]] void
__llvm_libc_mmix_linux_start(uintptr_t *stack) {
  LIBC_NAMESPACE::mmix::ProcessArgs args;
  if (!LIBC_NAMESPACE::mmix::parse_process_args(stack, args))
    __llvm_libc_mmix_linux_start_fail();
  __llvm_libc_mmix_linux_init_guard(args.random);
  LIBC_NAMESPACE::mmix::publish_process_args(args);
  __llvm_libc_mmix_linux_run();
}
