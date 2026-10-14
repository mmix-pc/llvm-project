//===-- Startup TLS resolver ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/linux/mmix/dynamic_tls.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
[[noreturn]] void fatal_dynamic_tls() {
  static constexpr char message[] = "LLVM libc: invalid dynamic TLS service\n";
  // Best-effort raw diagnostic; never recurse through errno or stdio.
  __llvm_libc_mmix_syscall(SYS_write, 2, reinterpret_cast<long>(message),
                          sizeof(message) - 1, 0, 0, 0);
  __llvm_libc_mmix_syscall(SYS_exit_group, 127, 0, 0, 0, 0, 0);
  __builtin_trap();
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
