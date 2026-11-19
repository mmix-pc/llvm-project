//===-- MMIX Linux cancellation windows --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CANCELLATION_POINT_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CANCELLATION_POINT_H

#include "src/__support/macros/config.h"
#include <stdint.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
struct ThreadControl;
constexpr int CANCEL_SIGNAL = 32;
constexpr uint64_t CANCEL_MASK = uint64_t(1) << (CANCEL_SIGNAL - 1);
void initialize_cancellation_windows(ThreadControl &control);
enum class StartupSignalActions { NormalizeCaught, ResetByExec };
// Initial-thread startup only, before callbacks or publishing any worker.
// Installs notification handling without manufacturing an unwind root.
void initialize_cancellation_process(StartupSignalActions actions);
// Revoke before creating a peer or publishing a caught signal disposition.
void revoke_startup_io_fast_path();
// Quiescent fork child only; notification delivery remains masked by the caller.
bool restore_fork_cancellation(ThreadControl &control,
                               uint64_t expected_suppression = 1);
bool cancellation_private_block();
int cancellation_public_mask(int how, const uint64_t *set, uint64_t *old);
long cancellation_syscall(unsigned long number, long a1 = 0, long a2 = 0,
                          long a3 = 0, long a4 = 0, long a5 = 0, long a6 = 0);
struct SignalWaitResult {
  long value;
  bool cancellation_rejected;
};
#ifdef LIBC_MMIX_SIGNAL_WAIT
// Select only a live trusted handler in the opt-in provider. Untracked custom
// restorers and ordinary threads retain their previous waiting behavior.
bool signal_cancellation_wait_available();
// Explicit controlled futex, process and signal waits only. Audited I/O uses
// a separate selection in cancellation_syscall, not this whitelist.
long signal_cancellation_wait(unsigned long number, long a1 = 0, long a2 = 0,
                              long a3 = 0, long a4 = 0, long a5 = 0,
                              long a6 = 0);
// Per-call provenance lets sigwait retry ordinary EINTR without restarting a
// wait abandoned by rejected handler cancellation. No shared sticky state.
SignalWaitResult signal_cancellation_wait_result(
    unsigned long number, long a1, long a2, long a3, long a4);
// Called masked after retiring a trusted default application signal boundary.
void rearm_signal_cancellation();
#endif

inline SignalWaitResult cancellation_signal_wait(unsigned long number, long a1,
                                                long a2, long a3 = 0,
                                                long a4 = 0) {
#ifdef LIBC_MMIX_SIGNAL_WAIT
  if (signal_cancellation_wait_available())
    return signal_cancellation_wait_result(number, a1, a2, a3, a4);
#endif
  return {cancellation_syscall(number, a1, a2, a3, a4), false};
}

// The controlled leaf checks pending cancellation before entering the kernel.
// Successful results and copy-out errors can consume child state; neither is
// converted to cancellation after the kernel returns.
inline long cancellation_process_wait(unsigned long number, long a1 = 0,
                                      long a2 = 0, long a3 = 0, long a4 = 0,
                                      long a5 = 0, long a6 = 0) {
#ifdef LIBC_MMIX_SIGNAL_WAIT
  if (signal_cancellation_wait_available())
    return signal_cancellation_wait(number, a1, a2, a3, a4, a5, a6);
#endif
  return cancellation_syscall(number, a1, a2, a3, a4, a5, a6);
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
