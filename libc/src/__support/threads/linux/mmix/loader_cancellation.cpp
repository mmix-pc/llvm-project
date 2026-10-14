//===-- Loader cancellation scope -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader_cancellation.h"
#include "cancellation.h"
#include "hdr/errno_macros.h"
#include <pthread.h>

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
namespace {
long enter(uint64_t *saved) {
  int previous;
  int error = set_cancel_state(PTHREAD_CANCEL_DISABLE, &previous);
  if (!error)
    *saved = static_cast<uint64_t>(previous);
  return -error;
}
long leave(uint64_t saved) {
  if (saved != PTHREAD_CANCEL_ENABLE && saved != PTHREAD_CANCEL_DISABLE)
    return -EINVAL;
  // Unlike notification masking alone, the disabled state also suppresses
  // explicit testcancel/cancellation leaves inside loader callbacks.
  return -set_cancel_state(static_cast<int>(saved), nullptr);
}
} // namespace
loader::LoaderCancellation loader_cancellation_hooks() {
  return {enter, leave};
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
