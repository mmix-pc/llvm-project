//===-- MMIX shared saved-context entry ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ForcedContext.h"
#include "../UnwindMetadataPolicy.h"

extern "C" __attribute__((visibility("hidden"))) _Unwind_Reason_Code
__unw_forcedunwind_from_context(_Unwind_Exception *, _Unwind_Stop_Fn, void *,
                               unw_context_t *);

extern "C" __attribute__((visibility("protected"))) _Unwind_Reason_Code
__libunwind_mmix_forcedunwind_from_context(_Unwind_Exception *exception,
                                         _Unwind_Stop_Fn stop, void *argument,
                                         unw_context_t *context) {
  if (__unw_metadata_policy_current() != UNW_METADATA_TERMINAL)
    return _URC_FATAL_PHASE2_ERROR;
  return __unw_forcedunwind_from_context(exception, stop, argument, context);
}
