//===-- Validated character category data -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_CTYPE_DATA_H
#define LLVM_LIBC_SRC_LOCALE_CTYPE_DATA_H

#include "src/locale/locale_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Unicode classification/case views, not a codec or a complete LC_CTYPE.
// The caller retains the immutable LocaleDataFile while using these tables.
class CTypeLocaleData {
public:
  enum Class : unsigned {
    UPPER,
    LOWER,
    ALPHA,
    DIGIT,
    SPACE,
    CNTRL,
    PUNCT,
    GRAPH,
    PRINT,
    XDIGIT,
    BLANK,
    CLASS_COUNT
  };
  enum Mapping : unsigned { TOUPPER, TOLOWER, TOTITLE, MAPPING_COUNT };
  // Category 0: class keys 1..11, case-map keys 0x101..0x103.
  static ErrorOr<CTypeLocaleData> read(const LocaleDataView &file);
  bool contains(Class kind, uint32_t scalar) const;
  uint32_t map(Mapping kind, uint32_t scalar) const;

private:
  LocaleDataView::Item classes[CLASS_COUNT];
  LocaleDataView::Item mappings[MAPPING_COUNT];
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_LOCALE_CTYPE_DATA_H
