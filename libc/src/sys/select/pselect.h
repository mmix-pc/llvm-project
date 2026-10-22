//===-- Implementation header for pselect -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SYS_SELECT_PSELECT_H
#define LLVM_LIBC_SRC_SYS_SELECT_PSELECT_H

#include "hdr/types/fd_set.h"
#include "hdr/types/sigset_t.h"
#include "hdr/types/struct_timespec.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
int pselect(int nfds, fd_set *__restrict read_set, fd_set *__restrict write_set,
            fd_set *__restrict error_set, const timespec *__restrict timeout,
            const sigset_t *__restrict mask);
} // namespace LIBC_NAMESPACE_DECL

#endif
