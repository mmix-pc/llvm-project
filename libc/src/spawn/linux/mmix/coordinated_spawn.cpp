//===-- Coordinated MMIX exec-only spawn ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "coordinated_spawn.h"
#include "src/__support/threads/linux/mmix/fork_runtime.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
long coordinated_spawn_clone() {
  ForkOperation operation;
  if (operation.error())
    return -operation.error();
  // Spawn deliberately does not run atfork callbacks. In particular, popen
  // holds its descriptor registry across this snapshot and child creation.
  int error = operation.prepare_runtime();
  if (!error)
    error = operation.freeze_registry();
  long result = error ? -error : operation.create_child();
  operation.restore_runtime(result == 0);
  // Destroy the operation in both processes before returning to raw child
  // setup. Its saved mask was already fully blocked by the spawn caller.
  return result;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
