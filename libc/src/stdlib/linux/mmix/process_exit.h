//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_STDLIB_LINUX_MMIX_PROCESS_EXIT_H
#define LLVM_LIBC_SRC_STDLIB_LINUX_MMIX_PROCESS_EXIT_H
#include "hdr/types/pid_t.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {
// PLATFORM elects one normal/quick exit owner without runtime locks; recursive
// exit is immediate. Quick exit also suppresses cancellation without the loader.
void begin_process_exit(int status, bool quick = false);

// Private coordinated-fork hooks, not a complete admission certificate. Query
// with application signals blocked, then independently prove an ordinary active
// unwind root and a Running, registered caller under the registry snapshot.
// Only the process-lifetime/main owner, after its TLS/TSS drain has returned
// and while exit_process is invoking process callbacks, yields a positive TID.
pid_t normal_exit_fork_owner();

// Child-only, after transport publishes the new TID and coordinated registry
// recovery preserves the caller's CLEANUP_CLAIMED state and callback progress.
// The snapshot must have passed the query and ordinary-root checks above.
// Does not acquire/release the process claim, restart callbacks, or repair
// roots.
bool rebind_normal_exit_fork_owner(pid_t previous_tid);
} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
