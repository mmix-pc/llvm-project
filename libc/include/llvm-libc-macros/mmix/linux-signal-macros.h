//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_MMIX_LINUX_SIGNAL_MACROS_H
#define LLVM_LIBC_MACROS_MMIX_LINUX_SIGNAL_MACROS_H

// Software-stack sizes; signal register backing is a separate kernel domain.
#define MINSIGSTKSZ 32768
#define SIGSTKSZ 65536
#define SS_AUTODISARM (1U << 31)

#endif // LLVM_LIBC_MACROS_MMIX_LINUX_SIGNAL_MACROS_H
