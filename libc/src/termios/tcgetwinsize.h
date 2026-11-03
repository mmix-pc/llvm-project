//===-- Implementation header for tcgetwinsize -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_TERMIOS_TCGETWINSIZE_H
#define LLVM_LIBC_SRC_TERMIOS_TCGETWINSIZE_H

#include "hdr/types/struct_winsize.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

int tcgetwinsize(int fd, struct winsize *winsize_p);

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_TERMIOS_TCGETWINSIZE_H
