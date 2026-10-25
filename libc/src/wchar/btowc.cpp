//===-- Implementation of btowc -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/btowc.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"
#include "src/__support/wchar/restartable_converter.h"

#include "hdr/types/wint_t.h"
#include "hdr/wchar_macros.h" // for WEOF.

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(wint_t, btowc, (int c)) {
  if (c > 255 || c < 0)
    return WEOF;
  internal::mbstate state{};
  uint8_t byte = static_cast<uint8_t>(c);
  uint32_t scalar = 0;
  auto result = internal::RestartableConverter::current().decode(
      &scalar, {&byte, 1}, state);
  if (!result || *result == size_t(-2))
    return WEOF;
  return static_cast<wint_t>(scalar);
}

} // namespace LIBC_NAMESPACE_DECL
