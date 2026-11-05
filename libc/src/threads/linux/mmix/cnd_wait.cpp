//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/threads/cnd_wait.h"
#include "src/pthread/linux/mmix/cancellation_condition.h"

namespace LIBC_NAMESPACE_DECL {

static_assert(sizeof(CndVar) == sizeof(cnd_t) &&
              alignof(CndVar) == alignof(cnd_t));
static_assert(sizeof(Mutex) == sizeof(mtx_t) &&
              alignof(Mutex) == alignof(mtx_t));

LLVM_LIBC_FUNCTION(int, cnd_wait, (cnd_t * cond, mtx_t *mutex)) {
  int result =
      mmix::condition_wait_impl(reinterpret_cast<CndVar *>(cond),
                                reinterpret_cast<Mutex *>(mutex), cpp::nullopt);
  return result == 0 ? thrd_success : thrd_error;
}

} // namespace LIBC_NAMESPACE_DECL
