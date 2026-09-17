//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Threading.h"
#include "src/__support/threads/thread.h"
#include "hdr/errno_macros.h"

#if LIBC_THREAD_MODE != LIBC_THREAD_MODE_SINGLE
#  error "MMIX Linux C++ keys require the selected single-thread libc state"
#endif

// FIXME: Replace this main-thread bridge with pthread keys when TLS is available.
// Reuse libc's actual key/destructor manager and normal-exit CRT cleanup.
int __llvm_libc_mmix_cxx_key_create(tss_t* key, void (*dtor)(void*)) {
  if (!LIBC_NAMESPACE::current_thread().attrib)
    return EINVAL;
  auto value = LIBC_NAMESPACE::new_tss_key(dtor);
  if (!value)
    return EAGAIN;
  *key = *value;
  return 0;
}
void* __llvm_libc_mmix_cxx_key_get(tss_t key) { return LIBC_NAMESPACE::get_tss_value(key); }
int __llvm_libc_mmix_cxx_key_set(tss_t key, void* value) {
  return LIBC_NAMESPACE::set_tss_value(key, value) ? 0 : EINVAL;
}
