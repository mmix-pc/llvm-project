//===-- Implementation of wcsxfrm_l
//----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/wcsxfrm_l.h"
#include "src/__support/common.h"
#include "src/locale/collation.h"

namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(size_t, wcsxfrm_l,
                   (wchar_t *__restrict dest, const wchar_t *__restrict source,
                    size_t capacity, locale_t locale)) {
  return internal::collate_transform(dest, source, capacity,
                                     internal::explicit_collation(locale));
}
} // namespace LIBC_NAMESPACE_DECL
