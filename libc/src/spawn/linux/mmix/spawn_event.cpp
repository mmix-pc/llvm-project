//===-- Private MMIX spawn initial-image transport ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "spawn_event.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include <asm/unistd.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

static_assert(MMIX_SPAWN_VERSION == 1);
static_assert(sizeof(mmix_spawn_args) == 32 && alignof(mmix_spawn_args) == 8);
static_assert(__builtin_offsetof(mmix_spawn_args, version) == 0);
static_assert(__builtin_offsetof(mmix_spawn_args, reserved) == 4);
static_assert(__builtin_offsetof(mmix_spawn_args, event_fd) == 8);
static_assert(__builtin_offsetof(mmix_spawn_args, pidfd) == 16);
static_assert(__builtin_offsetof(mmix_spawn_args, reserved2) == 24);
static_assert(sizeof(int) == 4 && alignof(int) == 4);
static_assert(sizeof(unsigned long) == 8 && sizeof(void *) == 8);

long spawn_event_capability() {
  return syscall_impl(__NR_mmix_spawn, MMIX_SPAWN_QUERY, 0, 0, 0, 0, 0);
}

[[gnu::returns_twice, gnu::noinline]] long
create_spawn_event(SpawnEventHandles &output) {
  SpawnEventHandles scratch;
  mmix_spawn_args request{};
  request.version = MMIX_SPAWN_VERSION;
  request.event_fd = reinterpret_cast<unsigned long>(&scratch.event_fd);
  request.pidfd = reinterpret_cast<unsigned long>(&scratch.pidfd);
  long result = __llvm_libc_mmix_spawn_event_create(&request, sizeof(request));
  if (result > 0)
    output = scratch;
  return result;
}

long query_spawn_event(int event_fd) {
  return syscall_impl(__NR_mmix_spawn, MMIX_SPAWN_EVENT_QUERY,
                      static_cast<long>(event_fd), 0, 0, 0, 0);
}

long wait_spawn_event(int event_fd) {
  return syscall_impl(__NR_mmix_spawn, MMIX_SPAWN_EVENT_WAIT,
                      static_cast<long>(event_fd), 0, 0, 0, 0);
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
