//===-- MMIX Linux priority protection accounting -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "cancellation_point.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "priority_protection_state.h"
#include "src/__support/CPP/limits.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include <sys/syscall.h>
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
  uint64_t saved_mask = 0;
  explicit Guard(PriorityProtectionState &value) : state(value) {
    // Async landing must not abandon a held ledger guard or partial update.
    uint64_t mask = CANCEL_MASK;
    if (syscall_impl<long>(SYS_rt_sigprocmask, SIG_BLOCK, &mask, &saved_mask,
                           sizeof(mask)))
      __builtin_trap();
    if (state.guard.lock() != MutexError::NONE)
      __builtin_trap();
  }
  ~Guard() {
    if (state.guard.unlock() != MutexError::NONE)
      __builtin_trap();
    if (syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK, &saved_mask, 0,
                           sizeof(saved_mask)))
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
int prepare(PriorityProtectionState &state, bool condition_recovery = false) {
  if (state.restore_pending && !condition_recovery) {
    if (int error = apply(state))
      return error;
  }
  int policy, priority;
  if (int error = priority_kernel_read(policy, priority))
    return error;
  if (empty(state) && !state.restore_pending) {
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
int priority_enter(int ceiling, bool check_base, bool condition_recovery) {
  auto &state = priority_protection_state();
  Guard guard(state);
  if (int error = prepare(state, condition_recovery))
    return error;
  if (check_base && state.base > ceiling)
    return EINVAL;
  if (ceiling < 1 || ceiling > 127)
    return EINVAL;
  if (state.contributions[ceiling] == cpp::numeric_limits<unsigned>::max())
    return EAGAIN;
  ++state.contributions[ceiling];
  // A condition recovery must not lower and then re-raise a retained boost.
  // Reconstruct ownership at a verified sufficient priority, preserving debt
  // if another lowering is still needed. Ordinary acquisitions do not use it.
  if (condition_recovery && state.restore_pending &&
      desired(state) <= state.applied) {
    state.restore_pending = desired(state) != state.applied;
    return 0;
  }
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
int priority_replace(int previous, int replacement, bool condition_recovery) {
  auto &state = priority_protection_state();
  Guard guard(state);
  if (int error = prepare(state, condition_recovery))
    return error;
  if (!state.contributions[previous])
    __builtin_trap();
  if (state.contributions[replacement] == cpp::numeric_limits<unsigned>::max())
    return EAGAIN;
  --state.contributions[previous];
  ++state.contributions[replacement];
  if (condition_recovery && state.restore_pending &&
      desired(state) <= state.applied) {
    state.restore_pending = desired(state) != state.applied;
    return 0;
  }
  if (int error = apply(state)) {
    --state.contributions[replacement];
    ++state.contributions[previous];
    state.restore_pending = desired(state) != state.applied;
    return error;
  }
  return 0;
}
#ifdef LIBC_MMIX_PTHREAD_SCHEDULING
int scheduling_validate(int policy, int priority) {
  if (policy == 0)
    return priority == 0 ? 0 : EINVAL;
  if (policy != 1 && policy != 2)
    return EINVAL;
  int low, high;
  if (int error = priority_range(low, high))
    return error;
  return priority < low || priority > high ? EINVAL : 0;
}
static int scheduling_refresh(PriorityProtectionState &state, int tid) {
  int policy, priority;
  if (int error = priority_kernel_read_for(tid, policy, priority))
    return error;
  if (empty(state) && !state.restore_pending) {
    state.policy = policy;
    state.base = state.applied = priority;
  } else if (policy != state.policy || priority != state.applied) {
    return EINVAL;
  }
  return 0;
}
int scheduling_read(PriorityProtectionState &state, int tid, int &policy,
                    int &priority) {
  Guard guard(state);
  if (int error = scheduling_refresh(state, tid))
    return error;
  policy = state.policy;
  priority = state.base;
  return 0;
}
int scheduling_write(PriorityProtectionState &state, int tid, int policy,
                     int priority, bool priority_only) {
  Guard guard(state);
  if (int error = scheduling_refresh(state, tid))
    return error;
  if (priority_only)
    policy = state.policy;
  if (int error = scheduling_validate(policy, priority))
    return error;
  if (policy == 0 && !empty(state))
    return EINVAL;
  int target = priority;
  for (int i = 127; i > target; --i)
    if (state.contributions[i]) {
      target = i;
      break;
    }
  // Even an unchanged effective priority must receive a kernel permission check.
  if (int error = priority_kernel_set_for(tid, policy, target))
    return error;
  state.policy = policy;
  state.base = priority;
  state.applied = target;
  state.restore_pending = false;
  return 0;
}
#endif
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
