//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/threads/thrd_exit.h"
#include "src/__support/common.h"
#include "src/__support/threads/linux/mmix/unwind_root.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(void, thrd_exit, (int value)) {
  mmix::unwind_integer_to_root(value);
}

} // namespace LIBC_NAMESPACE_DECL
