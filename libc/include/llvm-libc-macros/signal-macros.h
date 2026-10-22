//===-- Definition of signal number macros --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_MACROS_SIGNAL_MACROS_H
#define LLVM_LIBC_MACROS_SIGNAL_MACROS_H

#if defined(__linux__)
#include "linux/signal-macros.h"
#elif defined(__mmix__)
#include "mmix/signal-macros.h"
#elif defined(__NVPTX__) || defined(__AMDGPU__) || defined(__SPIRV__)
#include "gpu/signal-macros.h"
#endif

#if defined(__linux__) && (defined(__x86_64__) || defined(__mmix__))
#if !defined(__LIBC_EXPLICIT_NAMESPACE) || defined(__LIBC_USE_POSIX)
#define __LIBC_HAS_UCONTEXT 1
#endif
#endif

#endif // LLVM_LIBC_MACROS_SIGNAL_MACROS_H
