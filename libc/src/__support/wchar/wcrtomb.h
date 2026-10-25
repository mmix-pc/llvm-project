//===-- Implementation header for wcrtomb ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC__SUPPORT_WCHAR_WCRTOMB_H
#define LLVM_LIBC_SRC__SUPPORT_WCHAR_WCRTOMB_H

#include "src/__support/error_or.h"
#include "src/__support/macros/null_check.h"
#include "src/__support/wchar/restartable_converter.h"
#include "src/__support/wchar/mbstate.h"

#include "hdr/errno_macros.h"
#include "hdr/types/char32_t.h"
#include "hdr/types/size_t.h"
#include "hdr/types/wchar_t.h"
#include "src/__support/common.h"
#include "src/__support/libc_assert.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

LIBC_INLINE ErrorOr<size_t>
wcrtomb(char *__restrict dest_ptr, wchar_t wc, mbstate *__restrict ps,
         const RestartableConverter &converter) {
  LIBC_CRASH_ON_NULLPTR(dest_ptr);
  LIBC_CRASH_ON_NULLPTR(ps);
  static_assert(sizeof(wchar_t) == 4);

  if (!converter.valid(*ps))
    return Error(EINVAL);
  return converter.encode(static_cast<uint32_t>(wc),
                          {reinterpret_cast<uint8_t *>(dest_ptr),
                           MAX_LOCALE_CHARACTER_BYTES}, *ps);
}

LIBC_INLINE ErrorOr<size_t> wcrtomb(char *__restrict dest_ptr, wchar_t wc,
                                    mbstate *__restrict ps) {
  return wcrtomb(dest_ptr, wc, ps, RestartableConverter::current());
}

LIBC_INLINE ErrorOr<size_t> wcrtomb(char *__restrict dest_ptr, wchar_t wc,
                                    mbstate *__restrict ps, bool use_utf8) {
  return wcrtomb(dest_ptr, wc, ps, RestartableConverter(use_utf8));
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC__SUPPORT_WCHAR_WCRTOMB_H
