//===-- Restartable locale string conversion --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_WCHAR_MBSNRTOWCS_H
#define LLVM_LIBC_SRC___SUPPORT_WCHAR_MBSNRTOWCS_H

#include "hdr/types/size_t.h"
#include "hdr/types/wchar_t.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/null_check.h"
#include "src/__support/wchar/mbstate.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

ErrorOr<size_t> mbsnrtowcs(wchar_t *__restrict dst, const char **__restrict src,
                           size_t max_src_bytes, size_t max_dst_chars,
                           mbstate *__restrict state);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
#endif
