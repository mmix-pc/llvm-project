//===-- MMIX Linux post-TP publication ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "tls_handoff.h"
#include "tls_startup.h"

extern "C" {
[[noreturn, gnu::visibility("hidden")]] void
__llvm_libc_mmix_linux_start_fail();
[[noreturn, gnu::visibility("hidden")]] void __llvm_libc_mmix_linux_run();
}

extern "C" [[noreturn, gnu::visibility("hidden")]] void
__llvm_libc_mmix_linux_post_tls(LIBC_NAMESPACE::mmix::StartupState *state) {
  using namespace LIBC_NAMESPACE;
  if (!state || !internal::activate_main_thread(state->thread))
    __llvm_libc_mmix_linux_start_fail();
  mmix::publish_process_args(state->args);
  __llvm_libc_mmix_linux_run();
}
