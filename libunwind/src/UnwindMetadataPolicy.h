//===-- Private unwind metadata policy ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LIBUNWIND_UNWIND_METADATA_POLICY_H
#define LIBUNWIND_UNWIND_METADATA_POLICY_H

#include <stdint.h>

// Private ELF runtime integration, not part of the public cursor/context ABI.
// Build UnwindMetadataPolicy.cpp and all cursor providers with
// _LIBUNWIND_USE_METADATA_POLICY. The provider must be a startup TLS module;
// its private entrypoint imports must be bound before signal-safe use.
enum {
  UNW_METADATA_ORDINARY = 0,
  UNW_METADATA_PREFLIGHT = 1,
  UNW_METADATA_TERMINAL = 2
};

struct UnwindMetadataPolicyToken {
  uintptr_t owner;
  uintptr_t generation;
  uintptr_t parent;
  unsigned previous_mode;
};

#ifdef __cplusplus
extern "C" {
#endif

// The target must block application signal delivery during transitions, as it
// does for publishing its signal-boundary records. There is no allocation,
// callback, dynamic TLS resolution, or pointer to caller storage in the TLS
// state. Tokens must be zero-initialized, are single-use and thread-affine, and
// must be ended in LIFO order. Initialize every participating cursor inside
// the scope; an older cursor may hold metadata from an unsafe lookup.
int __unw_metadata_policy_enter(struct UnwindMetadataPolicyToken *);
int __unw_metadata_policy_commit(const struct UnwindMetadataPolicyToken *);
int __unw_metadata_policy_leave(struct UnwindMetadataPolicyToken *);
unsigned __unw_metadata_policy_current(void);

#ifdef __cplusplus
}

namespace libunwind {
inline unsigned currentUnwindMetadataPolicy() {
#if defined(_LIBUNWIND_USE_METADATA_POLICY)
  return __unw_metadata_policy_current();
#else
  return UNW_METADATA_ORDINARY;
#endif
}
} // namespace libunwind
#endif

// Deliberately not an RAII scope: phase2 resume abandons the initiating C++
// frame without running its destructor. Enter before cursor initialization or
// seeding, keep the policy active across copies, personalities, landing pads,
// _Unwind_Resume and parent-context seeding, and leave only after all affected
// cursors/observers have been retired. A nonlocal abandonment must explicitly
// leave while the token is still available, or carry the token by value into
// independently surviving continuation storage. Never restore ordinary policy
// just before jumpto. A terminal owner must not return to an abandoned wait.
//
// Preflight rejects unavailable metadata with an error, but does not establish
// the safety of target section discovery, personalities or CFI expressions.
// Those retain their own admission/ownership checks. Commit is only for an
// already acquired terminal owner: missing required metadata then traps rather
// than invoking a lock/allocator fallback or reporting successful end-of-stack.
// No metadata pointers may outlive their separate image-ownership leases.
// This opt-in currently controls the DWARF cursor path on ELF. In particular,
// it is not permission to use a platform section finder with its own locks,
// frame-header cache, lazy dynamic loader queries, or allocating CFI execution.

#endif
