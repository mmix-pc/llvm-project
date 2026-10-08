//===-- MMIX Linux priority protection state ---------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_PRIORITY_PROTECTION_STATE_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_PRIORITY_PROTECTION_STATE_H
#include "robust_mutex.h"
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
struct PriorityProtectionState {
  // A leased peer may inspect this state when scheduling APIs are connected.
  // The guard itself needs PI: changing priority while holding a raw guard
  // would introduce an unprotected priority-inversion window.
  RobustMutex guard{false, false, false, true, false};
  unsigned contributions[128]{};
  int policy = -1, base = 0, applied = 0;
  bool restore_pending = false;
};
PriorityProtectionState &priority_protection_state();
int priority_range(int &minimum, int &maximum);
int priority_enter(int ceiling, bool check_base = true,
                   bool condition_recovery = false);
int priority_leave(int ceiling);
int priority_replace(int previous, int replacement,
                     bool condition_recovery = false);
// Kernel transport is separate from the ownership/state algorithm.
int priority_kernel_read(int &policy, int &priority);
int priority_kernel_set(int policy, int priority);
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
