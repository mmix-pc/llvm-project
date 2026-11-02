//===-- MMIX private saved-context entry -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LIBUNWIND_MMIX_FORCED_CONTEXT_H
#define LIBUNWIND_MMIX_FORCED_CONTEXT_H

#include <libunwind.h>
#include <unwind.h>

#ifdef __cplusplus
extern "C" {
#endif
// Matched private provider only, under UNW_METADATA_PREFLIGHT. Initialize at the
// saved context's exact current PC, not a return-address-adjusted PC. Other
// policies return UNW_EINVAL; initializer errors are forwarded unchanged.
// This neither starts forced unwinding nor acquires a terminal claim. The caller
// owns the saved chain and image lifetimes; signal ancestry is not inferred.
int __libunwind_mmix_init_saved_cursor(unw_cursor_t *, unw_context_t *);

// Matched private provider only. The caller owns the saved chain, has committed
// its restricted metadata policy, and uses the registered managed stop. It must
// finish the child cursor before a nonlocal parent transfer. No terminal claim,
// signal ancestry or image lifetime can be inferred from a context alone.
_Unwind_Reason_Code __libunwind_mmix_forcedunwind_from_context(
    struct _Unwind_Exception *, _Unwind_Stop_Fn, void *, unw_context_t *);
#ifdef __cplusplus
}
#endif

#endif
