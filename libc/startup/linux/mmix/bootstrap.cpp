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
#if LIBC_THREAD_MODE != LIBC_THREAD_MODE_SINGLE
#include "tls_handoff.h"
#include "tls_startup.h"
#endif

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
  app.tls = args.tls;
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

extern "C" [[noreturn, gnu::visibility("hidden")]] void
__llvm_libc_mmix_linux_start(uintptr_t *stack) {
#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_SINGLE
  LIBC_NAMESPACE::mmix::ProcessArgs args;
  // The TLS-free compatibility configuration cannot initialize a TLS image.
  if (!LIBC_NAMESPACE::mmix::parse_process_args(stack, args) || args.has_tls)
    __llvm_libc_mmix_linux_start_fail();
  __llvm_libc_mmix_linux_init_guard(args.random);
  LIBC_NAMESPACE::mmix::publish_process_args(args);
  __llvm_libc_mmix_linux_run();
#else
  auto *state = LIBC_NAMESPACE::mmix::prepare_tls_startup(stack);
  if (!state)
    __llvm_libc_mmix_linux_start_fail();
  __llvm_libc_mmix_linux_init_guard(state->args.random);
  __llvm_libc_mmix_linux_tls_handoff(state->tls.tp, state);
#endif
}
