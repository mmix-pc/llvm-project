//===-- Implementation of wcsxfrm ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/wcsxfrm.h"
#include "src/locale/collation.h"

#include "hdr/types/size_t.h"
#include "hdr/types/wchar_t.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(size_t, wcsxfrm,
                   (wchar_t *__restrict dest, const wchar_t *__restrict src,
                    size_t n)) {
  return internal::collate_transform(dest, src, n,
                                     internal::current_collation());
}

} // namespace LIBC_NAMESPACE_DECL
