//===-- TLS-free shared libc preparation ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "shared_startup.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
LIBC_CONSTINIT internal::MainThreadState shared_main;
static bool attempted;

static long prepare(const loader::StartupProcessView *process, void **state) {
  if (attempted || !process || !state || *state || process->page_size != 8192 ||
      !process->original_sp || !process->random)
    return EINVAL;
  attempted = true;
  long tid = syscall_impl(SYS_gettid);
  if (tid <= 0 || tid > cpp::numeric_limits<int>::max())
    return tid < 0 ? -tid : EINVAL;
  shared_main.attributes.tid = static_cast<int>(tid);
  *state = &shared_main;
  return 0;
}

static void abandon(void *state) {
  // No allocation or publication occurs in prepare. Never reuse a failed
  // process startup, or touch TLS on this pre-TP rollback path.
  if (state != &shared_main)
    __builtin_trap();
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

extern "C" __attribute__((visibility("protected")))
const LIBC_NAMESPACE::loader::LibcBootstrap __llvm_libc_loader_bootstrap = {
    sizeof(LIBC_NAMESPACE::loader::LibcBootstrap),
    LIBC_NAMESPACE::mmix::prepare, LIBC_NAMESPACE::mmix::adopt_shared_main,
    LIBC_NAMESPACE::mmix::abandon};
