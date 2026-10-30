//===-- Linux kernel notification record -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_KERNEL_SIGEVENT_H
#define LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_KERNEL_SIGEVENT_H

#include "hdr/signal_macros.h"
#include "hdr/types/pid_t.h"
#include "hdr/types/struct_sigevent.h"
#include "src/__support/common.h"

namespace LIBC_NAMESPACE_DECL {
namespace linux_syscalls {

// Linux kernel ABI layout for struct sigevent (64 bytes total):
// https://github.com/torvalds/linux/blob/master/include/uapi/asm-generic/siginfo.h
// https://man7.org/linux/man-pages/man2/timer_create.2.html
struct KernelSigevent {
  static constexpr unsigned PAD_SIZE =
      (64 - sizeof(int) * 2 - sizeof(sigval)) / sizeof(int);
  sigval sigev_value;
  int sigev_signo;
  int sigev_notify;
  union {
    int _pad[PAD_SIZE];
    pid_t _tid;
    struct {
      void (*_function)(sigval);
      void *_attribute;
    } _sigev_thread;
  } _sigev_un;

  LIBC_INLINE KernelSigevent() = default;

  LIBC_INLINE KernelSigevent(const sigevent &sev) : _sigev_un{} {
    sigev_value = sev.sigev_value;
    sigev_signo = sev.sigev_signo;
    sigev_notify = sev.sigev_notify;
    for (unsigned i = 0; i < PAD_SIZE; ++i)
      _sigev_un._pad[i] = 0;
    // Linux kernel treats SIGEV_THREAD_ID as a bitwise mask, while the
    // remaining POSIX values as distinct enums:
    // https://github.com/torvalds/linux/blob/master/kernel/time/posix-timers.c#L405
    // https://github.com/torvalds/linux/blob/master/kernel/time/posix-timers.c#L525
    if ((sev.sigev_notify & SIGEV_THREAD_ID) != 0) {
#if !defined(LIBC_FULL_BUILD) && defined(__GLIBC__)
      _sigev_un._tid = sev._sigev_un._tid;
#else
      _sigev_un._tid = sev.sigev_notify_thread_id;
#endif
    } else if (sev.sigev_notify == SIGEV_THREAD) {
      _sigev_un._sigev_thread._function = sev.sigev_notify_function;
      _sigev_un._sigev_thread._attribute = sev.sigev_notify_attributes;
    }
  }
};

static_assert(sizeof(KernelSigevent) == 64);

} // namespace linux_syscalls
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_OSUTIL_LINUX_KERNEL_SIGEVENT_H
