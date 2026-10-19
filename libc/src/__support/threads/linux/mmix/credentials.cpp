//===-- MMIX Linux process credential rendezvous --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "credentials.h"
#include "hdr/errno_macros.h"
#include "hdr/signal_macros.h"
#include "hdr/types/siginfo_t.h"
#include "lifecycle.h"
#include "src/__support/OSUtil/linux/mmix/syscall.h"
#include "src/__support/libc_errno.h"
#include "src/signal/linux/mmix/kernel_sigaction.h"
#include "src/signal/linux/mmix/signal_trampoline.h"
#include <linux/futex.h>
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {
struct alignas(8) GateMutex {
  RawMutex value;
};
LIBC_CONSTINIT GateMutex gate;
ThreadControl *members = nullptr;
struct Request {
  unsigned long number;
  long first, second, third;
  long process;
};
Request request{};

[[noreturn]] void inconsistent() {
  // No callbacks or allocation are safe after a partially committed change.
  syscall_impl(SYS_exit_group, 127);
  __builtin_trap();
}

void set_mask(uint64_t mask, uint64_t *old = nullptr) {
  if (syscall_impl<long>(SYS_rt_sigprocmask, SIG_SETMASK, &mask, old, 8))
    inconsistent();
}

void notification(int signal, siginfo_t *info, void *) {
  int saved_errno = libc_errno;
  auto *self = current_control;
  if (signal == CREDENTIAL_SIGNAL && info &&
      info->si_signo == CREDENTIAL_SIGNAL && info->si_code == SI_TKILL &&
      self && self->credential_pending.load(cpp::MemoryOrder::ACQUIRE) &&
      info->si_pid == request.process) {
    self->credential_result = syscall_impl<long>(request.number, request.first,
                                                 request.second, request.third);
    self->credential_pending.store(0, cpp::MemoryOrder::RELEASE);
    if (syscall_impl<long>(SYS_futex, &self->credential_pending.val,
                           FUTEX_WAKE_PRIVATE, 1, 0, 0, 0) < 0)
      inconsistent();
  }
  libc_errno = saved_errno;
}
} // namespace

CredentialGate::CredentialGate() {
  // A contender is itself a participant in the current transaction. Blocking
  // its notification while waiting for the gate would deadlock the owner.
  set_mask(UINT64_MAX & ~CREDENTIAL_MASK, &saved_mask);
  gate.value.lock();
  owned = true;
  set_mask(UINT64_MAX);
}

CredentialGate::~CredentialGate() { release(); }

void CredentialGate::release() {
  if (owned) {
    gate.value.unlock();
    owned = false;
    set_mask(saved_mask);
  }
}

bool initialize_credentials(ThreadControl &control) {
  KernelSigaction action{};
  action.sa_sigaction = notification;
  action.sa_flags = SA_SIGINFO | SA_RESTART | SA_RESTORER;
  action.sa_restorer = __llvm_libc_mmix_signal_trampoline;
  action.sa_mask = UINT64_MAX;
  if (syscall_impl<long>(SYS_rt_sigaction, CREDENTIAL_SIGNAL, &action, nullptr,
                         8))
    return false;
  register_credentials(control);
  uint64_t mask = CREDENTIAL_MASK;
  return syscall_impl<long>(SYS_rt_sigprocmask, SIG_UNBLOCK, &mask, nullptr,
                            8) == 0;
}

void register_credentials(ThreadControl &control) {
  if (control.credential_registered || control.attributes.tid <= 0)
    inconsistent();
  control.credential_next = members;
  control.credential_registered = true;
  members = &control;
}

void unregister_credentials(ThreadControl &control) {
  ThreadControl **link = &members;
  while (*link && *link != &control)
    link = &(*link)->credential_next;
  if (!*link || control.credential_pending.load(cpp::MemoryOrder::ACQUIRE))
    inconsistent();
  *link = control.credential_next;
  control.credential_next = nullptr;
  control.credential_registered = false;
}

void restore_credentials_after_fork(ThreadControl &control) {
  // The caller owns the copied gate. No peer or outstanding request is copied.
  members = &control;
  control.credential_next = nullptr;
  control.credential_registered = true;
  control.credential_pending.store(0, cpp::MemoryOrder::RELAXED);
}

bool prepare_credentials_fork() { return gate.value.try_lock(); }
void finish_credentials_fork() { gate.value.unlock(); }

long change_credentials(unsigned long number, long first, long second,
                        long third) {
  // An exec-only fork child has no peers and cannot create them. In particular,
  // _Fork may have copied the gate while an interrupted thread owned it.
  if (current_control && !current_control->fork_recovery.allows_thread_creation())
    return syscall_impl<long>(number, first, second, third);
  CredentialGate lock;
  if (!current_control || !current_control->credential_registered)
    return -ENOTSUP;
  long pid = syscall_impl<long>(SYS_getpid);
  if (pid < 0)
    return pid;
  request = {number, first, second, third, pid};
  // Membership and mappings cannot change while the gate is owned. All peers,
  // including internal helpers and threads completing cleanup, participate.
  for (auto *peer = members; peer; peer = peer->credential_next) {
    if (peer == current_control)
      continue;
    peer->credential_pending.store(1, cpp::MemoryOrder::RELEASE);
    if (syscall_impl<long>(SYS_tgkill, pid, peer->attributes.tid,
                           CREDENTIAL_SIGNAL))
      inconsistent();
  }
  for (auto *peer = members; peer; peer = peer->credential_next) {
    if (peer == current_control)
      continue;
    while (peer->credential_pending.load(cpp::MemoryOrder::ACQUIRE)) {
      long error = syscall_impl<long>(SYS_futex, &peer->credential_pending.val,
                                      FUTEX_WAIT_PRIVATE, 1, 0, 0, 0);
      if (error && error != -EINTR && error != -EAGAIN)
        inconsistent();
    }
  }
  // Changing the caller first could remove permission to notify its peers.
  long result = syscall_impl<long>(number, first, second, third);
  for (auto *peer = members; peer; peer = peer->credential_next)
    if (peer != current_control && peer->credential_result != result)
      inconsistent();
  return result;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
