//===-- MMIX Linux C cleanup records -----------------------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CLEANUP_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CLEANUP_H

#include <stdint.h>

// Private preparation interface. Owning functions require unwind tables and
// this attribute, including under LTO; noinline on push alone is insufficient.
// Callbacks must return normally; forced dispatch rejects nested registration.
#define MMIX_CLEANUP_FRAME __attribute__((noinline, disable_tail_calls))

struct MmixCleanupFrame {
  void *owner;
  uintptr_t ro, procedure, sp, limit;
  uint64_t chain, generation;
};
struct MmixCleanupRecord;
struct MmixCleanupLink {
  struct MmixCleanupRecord *record;
  struct MmixCleanupFrame frame;
  uint64_t serial;
};
struct MmixCleanupRecord {
  struct MmixCleanupLink previous;
  void (*callback)(void *);
  void *argument;
  uint64_t serial;
};

#ifdef __cplusplus
extern "C" {
#endif
__attribute__((visibility("hidden"))) void
__llvm_libc_mmix_thread_cleanup_push(struct MmixCleanupRecord *,
                                     void (*)(void *), void *);
__attribute__((visibility("hidden"))) void
__llvm_libc_mmix_thread_cleanup_pop(struct MmixCleanupRecord *, int);
#ifdef __cplusplus
}
#endif
#endif
