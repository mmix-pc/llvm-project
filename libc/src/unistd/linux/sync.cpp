//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/unistd/sync.h"
#include "src/__support/OSUtil/linux/syscall.h"
#include "src/__support/common.h"
#include <sys/syscall.h>

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(void, sync, ()) { syscall_impl<long>(SYS_sync); }
} // namespace LIBC_NAMESPACE_DECL
