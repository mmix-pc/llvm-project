//===-- Implementation header for msgctl ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SYS_MSG_MSGCTL_H
#define LLVM_LIBC_SRC_SYS_MSG_MSGCTL_H

#include "hdr/types/struct_msqid_ds.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

int msgctl(int msqid, int cmd, struct msqid_ds *buf);

} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_SYS_MSG_MSGCTL_H
