//===-- MMIX Linux unistd capability macros -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_MMIX_UNISTD_MACROS_H
#define LLVM_LIBC_MACROS_MMIX_UNISTD_MACROS_H

// FIXME: Advertise thread support once the MMIX Linux pthread provider and
// public interfaces are available. Internal mutex mode is not a capability.
#define _POSIX_THREADS (-1L)

#endif // LLVM_LIBC_MACROS_MMIX_UNISTD_MACROS_H
