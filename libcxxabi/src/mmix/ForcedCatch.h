//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#ifndef LIBCXXABI_MMIX_FORCED_CATCH_H
#define LIBCXXABI_MMIX_FORCED_CATCH_H

#include "../cxa_exception.h"

namespace __cxxabiv1 {
namespace mmix {
_LIBCXXABI_HIDDEN bool begin_forced_catch(_Unwind_Exception*, __cxa_eh_globals*, void*&);
_LIBCXXABI_HIDDEN bool end_forced_catch(__cxa_eh_globals*);
_LIBCXXABI_HIDDEN void rethrow_forced_catch(__cxa_eh_globals*);
} // namespace mmix
} // namespace __cxxabiv1
#endif
