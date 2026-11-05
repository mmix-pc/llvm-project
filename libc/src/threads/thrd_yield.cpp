//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/threads/thrd_yield.h"
#include "src/__support/common.h"
#include "src/sched/sched_yield.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(void, thrd_yield, ()) { LIBC_NAMESPACE::sched_yield(); }

} // namespace LIBC_NAMESPACE_DECL
