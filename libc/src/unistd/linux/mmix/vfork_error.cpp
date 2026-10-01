//===-- MMIX Linux vfork error return -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/types/pid_t.h"
#include "src/__support/libc_errno.h"
#include "src/__support/threads/linux/mmix/process_operation.h"

#if LIBC_THREAD_MODE == LIBC_THREAD_MODE_PLATFORM
extern "C" [[gnu::visibility("hidden")]] long __llvm_libc_mmix_vfork_begin() {
  return -LIBC_NAMESPACE::mmix::begin_process_operation();
}
extern "C" [[gnu::visibility("hidden")]] long
__llvm_libc_mmix_vfork_end(long result) {
  LIBC_NAMESPACE::mmix::end_process_operation();
  return result;
}
#endif

extern "C" [[gnu::visibility("hidden")]] pid_t
__llvm_libc_mmix_vfork_error(long result) {
  libc_errno = static_cast<int>(-result);
  return -1;
}
