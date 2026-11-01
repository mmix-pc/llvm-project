//===-- Private unwind metadata policy ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "UnwindMetadataPolicy.h"

#if !defined(__ELF__)
#error "The private metadata policy requires startup ELF TLS"
#endif

namespace {
struct PolicyState {
  uintptr_t next;
  uintptr_t active;
  unsigned mode;
};
thread_local PolicyState state __attribute__((tls_model("initial-exec"))) = {};
static_assert(__atomic_always_lock_free(sizeof(unsigned), nullptr),
              "metadata policy reads must not call a locking runtime");

bool current(const UnwindMetadataPolicyToken *token) {
  return token && token->owner == reinterpret_cast<uintptr_t>(&state) &&
         token->generation && token->generation == state.active &&
         token->parent < token->generation &&
         token->previous_mode <= UNW_METADATA_TERMINAL;
}
} // namespace

// These private hooks must bind to the same provider in libc and libunwind.
// Hidden header-local TLS copies would silently lose policy across the DSO
// boundary; protected definitions avoid that while allowing target imports.
extern "C" __attribute__((visibility("protected"))) unsigned
__unw_metadata_policy_current() {
  return __atomic_load_n(&state.mode, __ATOMIC_RELAXED);
}

extern "C" __attribute__((visibility("protected"))) int
__unw_metadata_policy_enter(UnwindMetadataPolicyToken *token) {
  if (!token || token->generation || state.next == UINTPTR_MAX)
    return 0;
  unsigned previous = __unw_metadata_policy_current();
  *token = {reinterpret_cast<uintptr_t>(&state), ++state.next, state.active,
            previous};
  state.active = token->generation;
  __atomic_store_n(&state.mode,
                   previous == UNW_METADATA_TERMINAL ? UNW_METADATA_TERMINAL
                                                     : UNW_METADATA_PREFLIGHT,
                   __ATOMIC_RELAXED);
  return 1;
}

extern "C" __attribute__((visibility("protected"))) int
__unw_metadata_policy_commit(const UnwindMetadataPolicyToken *token) {
  if (!current(token))
    return 0;
  __atomic_store_n(&state.mode, UNW_METADATA_TERMINAL, __ATOMIC_RELAXED);
  return 1;
}

extern "C" __attribute__((visibility("protected"))) int
__unw_metadata_policy_leave(UnwindMetadataPolicyToken *token) {
  if (!current(token))
    return 0;
  state.active = token->parent;
  __atomic_store_n(&state.mode, token->previous_mode, __ATOMIC_RELAXED);
  *token = {};
  return 1;
}
