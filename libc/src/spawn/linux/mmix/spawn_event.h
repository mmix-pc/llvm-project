//===-- Private MMIX spawn initial-image transport ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SPAWN_LINUX_MMIX_SPAWN_EVENT_H
#define LLVM_LIBC_SRC_SPAWN_LINUX_MMIX_SPAWN_EVENT_H

#include "src/__support/macros/config.h"
#include <asm/spawn.h>

// CREATE must not pass through an ordinary C syscall wrapper. The assembly
// leaf takes the request size from sizeof, not a second definition of the UAPI.
extern "C" [[gnu::returns_twice, gnu::visibility("hidden")]] long
__llvm_libc_mmix_spawn_event_create(const mmix_spawn_args *, unsigned long);

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

struct SpawnEventHandles {
  int event_fd = -1;
  int pidfd = -1;
};

// Raw capability bits or negative errno. No fallback or error-policy mapping.
long spawn_event_capability();

// Raw parent PID, child zero, or negative errno. Only a positive return grants
// ownership of both CLOEXEC handles and changes output. Failed CREATE may dirty
// kernel output slots; these stay private and must never be closed as handles.
// The child must enter raw setup/exec/exit, not use copied parent runtime state.
// Caller owns capability admission, credentials, masks, cancellation and reap.
[[gnu::returns_twice, gnu::noinline]] long
create_spawn_event(SpawnEventHandles &output);

// Single ordinary syscall attempts, with no cancellation point or errno write.
// QUERY returns PENDING/RELEASED; WAIT returns zero on release or negative errno.
// Caller retains fd ownership and handles EINTR/recheck with signal 33 enabled.
long query_spawn_event(int event_fd);
long wait_spawn_event(int event_fd);

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#endif
