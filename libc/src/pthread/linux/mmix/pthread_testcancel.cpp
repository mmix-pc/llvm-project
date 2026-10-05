//===-- MMIX Linux cancellation provider ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/pthread/pthread_testcancel.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/cancellation.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(void, pthread_testcancel, (void)) { mmix::test_cancel(); }
} // namespace LIBC_NAMESPACE_DECL
