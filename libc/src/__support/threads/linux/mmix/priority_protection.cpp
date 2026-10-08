//===-- MMIX Linux priority protection accounting -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "hdr/errno_macros.h"
#include "priority_protection_state.h"
#include "src/__support/CPP/limits.h"
#ifdef LIBC_FULL_BUILD
#include "lifecycle.h"
#endif
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
PriorityProtectionState &priority_protection_state() {
#ifdef LIBC_FULL_BUILD
  if (!current_control)
    __builtin_trap();
  return current_control->priority_protection;
#else
  static LIBC_THREAD_LOCAL PriorityProtectionState state;
  return state;
#endif
}
namespace {
struct Guard {
  PriorityProtectionState &state;
  explicit Guard(PriorityProtectionState &value) : state(value) {
    if (state.guard.lock() != MutexError::NONE)
      __builtin_trap();
  }
  ~Guard() {
    if (state.guard.unlock() != MutexError::NONE)
      __builtin_trap();
  }
};
int desired(const PriorityProtectionState &state) {
  for (int i = 127; i > state.base; --i)
    if (state.contributions[i])
      return i;
  return state.base;
}
bool empty(const PriorityProtectionState &state) {
  for (unsigned count : state.contributions)
    if (count)
      return false;
  return true;
}
int apply(PriorityProtectionState &state) {
  int target = desired(state);
  if (target == state.applied) {
    state.restore_pending = false;
    return 0;
  }
  int error = priority_kernel_set(state.policy, target);
  if (error) {
    state.restore_pending = true;
    return error;
  }
  state.applied = target;
  state.restore_pending = false;
  return 0;
}
int prepare(PriorityProtectionState &state) {
  if (state.restore_pending) {
    if (int error = apply(state))
      return error;
  }
  int policy, priority;
  if (int error = priority_kernel_read(policy, priority))
    return error;
  if (empty(state)) {
    state.policy = policy;
    state.base = state.applied = priority;
  } else if (policy != state.policy || priority != state.applied) {
    // Direct scheduler syscalls cannot bypass the owned contribution ledger.
    return EINVAL;
  }
  if (policy != 1 && policy != 2)
    return EINVAL; // FIFO or RR, never promote OTHER.
  return 0;
}
} // namespace
int priority_enter(int ceiling, bool check_base) {
  auto &state = priority_protection_state();
  Guard guard(state);
  if (int error = prepare(state))
    return error;
  if (check_base && state.base > ceiling)
    return EINVAL;
  if (ceiling < 1 || ceiling > 127)
    return EINVAL;
  if (state.contributions[ceiling] == cpp::numeric_limits<unsigned>::max())
    return EAGAIN;
  ++state.contributions[ceiling];
  if (int error = apply(state)) {
    --state.contributions[ceiling];
    state.restore_pending = desired(state) != state.applied;
    return error;
  }
  return 0;
}
int priority_leave(int ceiling) {
  auto &state = priority_protection_state();
  Guard guard(state);
  if (ceiling < 1 || ceiling > 127 || !state.contributions[ceiling])
    __builtin_trap();
  --state.contributions[ceiling];
  // Caller has already released ownership or withdrawn its failed reservation.
  // A failed lowering leaves a disclosed debt, never a fictitious success.
  return apply(state);
}
int priority_replace(int previous, int replacement) {
  auto &state = priority_protection_state();
  Guard guard(state);
  if (int error = prepare(state))
    return error;
  if (!state.contributions[previous])
    __builtin_trap();
  if (state.contributions[replacement] == cpp::numeric_limits<unsigned>::max())
    return EAGAIN;
  --state.contributions[previous];
  ++state.contributions[replacement];
  if (int error = apply(state)) {
    --state.contributions[replacement];
    ++state.contributions[previous];
    state.restore_pending = desired(state) != state.applied;
    return error;
  }
  return 0;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
