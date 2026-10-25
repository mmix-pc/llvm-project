//===-- Implementation of mbtowc -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/mbtowc.h"

#include "hdr/errno_macros.h"
#include "hdr/types/size_t.h"
#include "hdr/types/wchar_t.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "src/__support/wchar/mbrtowc.h"
#include "src/__support/wchar/mbstate.h"
#include "src/locale/encoding.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, mbtowc,
                   (wchar_t *__restrict pwc, const char *__restrict s,
                    size_t n)) {
  static LIBC_THREAD_LOCAL internal::mbstate internal_mbstate;
  if (s == nullptr) {
    internal_mbstate = {};
    return internal::locale_chinese_encoding() != nullptr;
  }
  size_t maximum = internal::locale_chinese_encoding() ? 8
                   : internal::locale_is_utf8()        ? 4
                                                       : 1;
  if (n > maximum)
    n = maximum;
  auto candidate = internal_mbstate;
  auto ret = internal::mbrtowc(pwc, s, n, &candidate);
  if (!ret.has_value() || ret.value() == size_t(-2)) {
    // Encoding failure
    libc_errno = EILSEQ;
    return -1;
  }
  internal_mbstate = candidate;
  return static_cast<int>(ret.value());
}

} // namespace LIBC_NAMESPACE_DECL
