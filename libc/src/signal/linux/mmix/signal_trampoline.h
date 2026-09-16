//===-- MMIX Linux signal trampoline -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SIGNAL_LINUX_MMIX_SIGNAL_TRAMPOLINE_H
#define LLVM_LIBC_SRC_SIGNAL_LINUX_MMIX_SIGNAL_TRAMPOLINE_H

// sa_restorer names delivery; the handler return entry is exactly four bytes
// later. Only the kernel may enter this function with the prepared signal state.
extern "C" __attribute__((visibility("hidden"))) void
__llvm_libc_mmix_signal_trampoline();

#endif
