//===-- Implementation for mbrtowc function ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/wchar/mbrtowc.h"
#include "hdr/errno_macros.h"
#include "hdr/types/size_t.h"
#include "hdr/types/wchar_t.h"
#include "src/__support/common.h"
#include "src/__support/error_or.h"
#include "src/__support/macros/config.h"
#include "src/__support/wchar/restartable_converter.h"
#include "src/__support/wchar/mbstate.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

ErrorOr<size_t> mbrtowc(wchar_t *__restrict pwc, const char *__restrict src_ptr,
                        size_t max_src_bytes, mbstate *__restrict ps) {
  if (src_ptr == nullptr) {
    src_ptr = "";
    max_src_bytes = 1;
    pwc = nullptr;
  }
  auto converter = RestartableConverter::current();
  if (!converter.valid(*ps))
    return Error(EINVAL);
  uint32_t scalar = 0;
  auto result = converter.decode(
      &scalar, {reinterpret_cast<const uint8_t *>(src_ptr), max_src_bytes}, *ps);
  if (result && *result != size_t(-2) && pwc)
    *pwc = static_cast<wchar_t>(scalar);
  return result;
}

} // namespace internal

} // namespace LIBC_NAMESPACE_DECL
