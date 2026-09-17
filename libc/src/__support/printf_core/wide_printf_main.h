//===-- Internal wide printf entry ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_PRINTF_CORE_WIDE_PRINTF_MAIN_H
#define LLVM_LIBC_SRC___SUPPORT_PRINTF_CORE_WIDE_PRINTF_MAIN_H

#include "src/__support/arg_list.h"
#include "src/__support/error_or.h"
#include "src/__support/printf_core/parser.h"
#include "src/__support/printf_core/wide_converter.h"

namespace LIBC_NAMESPACE_DECL {
namespace printf_core {

LIBC_INLINE ErrorOr<size_t> wide_printf_main(WideWriter *writer,
                                             const wchar_t *format,
                                             internal::ArgList &args) {
  if (writer->get_error() != WRITE_OK)
    return Error(-writer->get_error());
  Parser<internal::ArgList, wchar_t> parser(format, args);
  for (auto section = parser.get_next_section(); !section.raw_string.empty();
       section = parser.get_next_section()) {
    int result = convert_wide(writer, section);
    if (result < 0)
      return Error(-result);
  }
  return writer->get_chars_written();
}

} // namespace printf_core
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_PRINTF_CORE_WIDE_PRINTF_MAIN_H
