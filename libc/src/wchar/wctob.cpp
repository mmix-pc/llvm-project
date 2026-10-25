//===-- Implementation of wctob -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/wctob.h"
#include "src/__support/common.h"
#include "src/__support/macros/config.h"
#include "src/__support/wchar/restartable_converter.h"

#include "hdr/stdio_macros.h" // for EOF.
#include "hdr/types/wint_t.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(int, wctob, (wint_t c)) {
  internal::mbstate state{};
  uint8_t byte;
  auto result = internal::RestartableConverter::current().encode(
      static_cast<uint32_t>(c), {&byte, 1}, state);
  if (!result || *result != 1)
    return EOF;
  return byte;
}

} // namespace LIBC_NAMESPACE_DECL
