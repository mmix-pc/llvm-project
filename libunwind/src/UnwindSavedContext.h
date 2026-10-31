//===-- Saved-current-context capabilities -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LIBUNWIND_SAVED_CONTEXT_H
#define LIBUNWIND_SAVED_CONTEXT_H

#include "config.h"
#include "shadow_stack_unwind.h"

// A saved logical frame does not determine the active shadow-stack pop count.
// Other metadata/restore backends have not qualified exact-PC entry either.
#if defined(_LIBUNWIND_SUPPORT_DWARF_UNWIND) &&                            \
    !defined(_LIBUNWIND_SUPPORT_COMPACT_UNWIND) &&                         \
    !defined(_LIBUNWIND_SUPPORT_TBTAB_UNWIND) &&                           \
    !defined(_LIBUNWIND_SUPPORT_SEH_UNWIND) &&                             \
    !defined(_LIBUNWIND_ARM_EHABI) && !defined(__USING_SJLJ_EXCEPTIONS__) && \
    !defined(__wasm__) && !defined(_LIBUNWIND_USE_CET) &&                   \
    !defined(_LIBUNWIND_USE_GCS) && !defined(_LIBUNWIND_TRACE_RET_INJECT) && \
    !defined(_LIBUNWIND_TARGET_AARCH64_AUTHENTICATED_UNWINDING)
#define _LIBUNWIND_SUPPORT_SAVED_CONTEXT 1
#endif

#endif
