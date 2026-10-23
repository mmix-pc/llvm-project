//===-- String type specifier converters for scanf --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_STDIO_SCANF_CORE_STRING_CONVERTER_H
#define LLVM_LIBC_SRC_STDIO_SCANF_CORE_STRING_CONVERTER_H

#include "src/__support/CPP/limits.h"
#include "src/__support/ctype_utils.h"
#include "src/__support/macros/config.h"
#include "src/__support/wchar/mbrtowc.h"
#include "src/stdio/scanf_core/core_structs.h"
#include "src/stdio/scanf_core/reader.h"
#include "src/stdio/scanf_core/string_output.h"

#include <stddef.h>

namespace LIBC_NAMESPACE_DECL {
namespace scanf_core {

template <typename T, typename CharT>
int convert_string_to(Reader<T> *reader, const FormatSection &to_conv) {
  // %s "Matches a sequence of non-white-space characters"

  // %c "Matches a sequence of characters of exactly the number specified by the
  // field width (1 if no field width is present in the directive)"

  // %[ "Matches a nonempty sequence of characters from a set of expected
  // characters (the scanset)."
  size_t max_width = 0;
  if (to_conv.max_width > 0) {
    max_width = to_conv.max_width;
  } else {
    if (to_conv.conv_name == 'c') {
      max_width = 1;
    } else {
      max_width = cpp::numeric_limits<size_t>::max();
    }
  }

  StringOutput<CharT> output(to_conv);
  internal::mbstate state{};
  size_t count = 0;
  bool at_end = false;
  while (count < max_width) {
    int c = reader->getc();
    if (c < 0) {
      at_end = true;
      break;
    }
    auto matches = [&](int ch) {
      return to_conv.conv_name == 'c' ||
             (to_conv.conv_name == 's' &&
              !internal::isspace(static_cast<char>(ch))) ||
             (to_conv.conv_name == '[' && to_conv.scan_set.test(size_t(ch)));
    };
    if (!matches(c)) {
      reader->ungetc(c);
      break;
    }
    CharT value;
    if constexpr (sizeof(CharT) == sizeof(char)) {
      value = static_cast<CharT>(c);
    } else {
      for (;;) {
        char byte = static_cast<char>(c);
        auto result = internal::mbrtowc(&value, &byte, 1, &state);
        if (!result.has_value()) {
          libc_errno = result.error();
          return ENCODING_FAILURE;
        }
        if (result.value() != size_t(-2))
          break;
        c = reader->getc();
        if (c < 0 || !matches(c)) {
          reader->ungetc(c);
          if (c < 0 && reader->error())
            return FILE_READ_ERROR;
          libc_errno = EILSEQ;
          return ENCODING_FAILURE;
        }
      }
    }
    if (!output.append(value))
      return ALLOCATION_FAILURE;
    ++count;
  }
  if (to_conv.conv_name == 'c' && count != max_width)
    return FILE_READ_ERROR;
  if (!count)
    return at_end ? FILE_READ_ERROR : MATCHING_FAILURE;
  if (to_conv.conv_name != 'c' && !output.append(CharT(0)))
    return ALLOCATION_FAILURE;
  output.publish();
  return READ_OK;
}

template <typename T>
int convert_string(Reader<T> *reader, const FormatSection &to_conv) {
  if (to_conv.length_modifier == LengthModifier::l)
    return convert_string_to<T, wchar_t>(reader, to_conv);
  return convert_string_to<T, char>(reader, to_conv);
}

} // namespace scanf_core
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_STDIO_SCANF_CORE_STRING_CONVERTER_H
