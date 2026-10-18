//===-- MB_CUR_MAX implementation -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/locale/mb_cur_max.h"
#include "src/__support/common.h"
#include "src/locale/encoding.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(size_t, __ctype_get_mb_cur_max, ()) {
  return internal::locale_is_utf8() ? 4 : 1;
}
} // namespace LIBC_NAMESPACE_DECL
