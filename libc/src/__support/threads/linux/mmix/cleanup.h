//===-- MMIX Linux C cleanup records -----------------------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CLEANUP_H
#define LLVM_LIBC_SRC___SUPPORT_THREADS_LINUX_MMIX_CLEANUP_H

#include "include/llvm-libc-macros/mmix/pthread-cleanup.h"
#include <stdint.h>

// Private preparation interface. Owning functions require unwind tables and
// this attribute, including under LTO; noinline on push alone is insufficient.
// Callbacks must return normally; forced dispatch rejects nested registration.
#define MMIX_CLEANUP_FRAME __attribute__((noinline, disable_tail_calls))

typedef struct __llvm_libc_mmix_cleanup_frame MmixCleanupFrame;
typedef struct __llvm_libc_mmix_cleanup_link MmixCleanupLink;
typedef struct __llvm_libc_mmix_cleanup_record MmixCleanupRecord;
typedef struct __llvm_libc_mmix_cleanup_owner MmixCleanupOwner;

#ifdef __cplusplus
#include "src/__support/macros/config.h"
namespace LIBC_NAMESPACE_DECL {
namespace mmix {
// Signals are blocked. This snapshots metadata, not callback ownership, and
// rejects interruption of cleanup dispatch rather than publishing half-state.
bool snapshot_c_cleanup(MmixCleanupLink &);
bool c_cleanup_at_checkpoint(const MmixCleanupLink &);
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
#endif
#endif
