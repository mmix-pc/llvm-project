//===-- Starting point for scanf --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_STDIO_SCANF_CORE_SCANF_MAIN_H
#define LLVM_LIBC_SRC_STDIO_SCANF_CORE_SCANF_MAIN_H

#include "src/__support/arg_list.h"
#include "src/__support/macros/config.h"
#include "src/stdio/scanf_core/converter.h"
#include "src/stdio/scanf_core/core_structs.h"
#include "src/stdio/scanf_core/parser.h"
#include "src/stdio/scanf_core/reader.h"

#include <stddef.h>

namespace LIBC_NAMESPACE_DECL {
namespace scanf_core {

template <typename T>
int scanf_main(Reader<T> *reader, const char *__restrict str,
               internal::ArgList &args, int *conversion_status = nullptr) {
  Parser<internal::ArgList> parser(str, args);
  int ret_val = READ_OK;
  int conversions = 0;
  bool completed_conversion = false;
  for (FormatSection cur_section = parser.get_next_section();
       !cur_section.raw_string.empty() && ret_val == READ_OK;
       cur_section = parser.get_next_section()) {
    if (cur_section.has_conv) {
      ret_val = convert(reader, cur_section);
      if (ret_val == READ_OK)
        completed_conversion = true;
      // Completion and assignment are distinct: %n, %% and suppressed
      // conversions do not increment the number of assignments.
      if (cur_section.conv_name != 'n' && cur_section.conv_name != '%' &&
          (cur_section.flags & FormatFlags::NO_WRITE) == 0)
        conversions += ret_val == READ_OK ? 1 : 0;
    } else {
      ret_val = raw_match(reader, cur_section.raw_string);
    }
  }

  bool input_failure = ret_val == FILE_READ_ERROR ||
                       ret_val == ALLOCATION_FAILURE ||
                       ret_val == ENCODING_FAILURE;
  if (conversion_status)
    *conversion_status = ret_val;
  return input_failure && !completed_conversion ? -1 : conversions;
}

} // namespace scanf_core
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_STDIO_SCANF_CORE_SCANF_MAIN_H
