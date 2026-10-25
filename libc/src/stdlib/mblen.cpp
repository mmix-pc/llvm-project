//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Implementation of mblen.
///
//===----------------------------------------------------------------------===//

#include "src/stdlib/mblen.h"

#include "hdr/errno_macros.h"
#include "hdr/types/size_t.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "src/__support/wchar/mbrtowc.h"
#include "src/__support/wchar/mbstate.h"
#include "src/locale/encoding.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, mblen, (const char *s, size_t n)) {
  if (s == nullptr)
    return internal::locale_chinese_encoding() != nullptr;
  // POSIX specifies a fresh conversion, without affecting mbtowc's state.
  internal::mbstate internal_mbstate;
  size_t maximum = internal::locale_chinese_encoding() ? 8
                   : internal::locale_is_utf8()        ? 4
                                                       : 1;
  if (n > maximum)
    n = maximum;
  auto ret = internal::mbrtowc(nullptr, s, n, &internal_mbstate);
  if (!ret.has_value() || ret.value() == size_t(-2)) {
    if (!ret.has_value())
      libc_errno = EILSEQ;
    return -1;
  }
  return static_cast<int>(ret.value());
}

} // namespace LIBC_NAMESPACE_DECL
