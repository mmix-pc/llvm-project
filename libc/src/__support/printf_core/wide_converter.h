//===-- Wide printf conversion dispatch ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_PRINTF_CORE_WIDE_CONVERTER_H
#define LLVM_LIBC_SRC___SUPPORT_PRINTF_CORE_WIDE_CONVERTER_H

#include "src/__support/printf_core/converter.h"
#include "src/__support/printf_core/ptr_converter.h"
#include "src/__support/printf_core/wide_writer.h"
#include "src/__support/printf_core/write_int_converter.h"

namespace LIBC_NAMESPACE_DECL {
namespace printf_core {

LIBC_INLINE int convert_wide(WideWriter *writer,
                             const BasicFormatSection<wchar_t> &section) {
#if defined(LIBC_COPT_PRINTF_MODULAR) &&                                       \
    !defined(LIBC_COPT_PRINTF_DISABLE_FLOAT)
  LIBC_INLINE_ASM(".reloc ., BFD_RELOC_NONE, __printf_float");
#endif
  if (writer->get_error() != WRITE_OK)
    return writer->get_error();
  if (!section.has_conv)
    return writer->write(section.raw_string);

  WideNumericWriter numeric_writer(*writer);
#if defined(LIBC_INTERNAL_PRINTF_CONVERT_FLOAT128)
  if (section.length_modifier == LengthModifier::Q)
    return writer->write(section.raw_string);
#endif
  switch (section.conv_name) {
  case '%':
    return writer->write(L'%');
  case 'd':
  case 'i':
  case 'u':
  case 'o':
  case 'x':
  case 'X':
  case 'b':
  case 'B':
    return convert_int(&numeric_writer, section);
  case 'p':
    return convert_pointer(&numeric_writer, section);
#ifndef LIBC_COPT_PRINTF_DISABLE_FLOAT
  case 'f':
  case 'F':
  case 'e':
  case 'E':
  case 'g':
  case 'G':
#ifdef LIBC_COPT_PRINTF_HEX_LONG_DOUBLE
    if (section.length_modifier == LengthModifier::L)
      return convert_float_hex_exp(&numeric_writer, section);
#endif
    [[fallthrough]];
  case 'a':
  case 'A':
    return convert_float(&numeric_writer, section);
#endif
#ifndef LIBC_COPT_PRINTF_DISABLE_WRITE_INT
  case 'n':
    return convert_write_int(&numeric_writer, section);
#endif
  default:
    return writer->write(section.raw_string);
  }
}

} // namespace printf_core
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_PRINTF_CORE_WIDE_CONVERTER_H
