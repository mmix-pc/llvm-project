//===-- Validated ISO-2022-CN mapping tables ---------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_LOCALE_ENCODING_DATA_H
#define LLVM_LIBC_SRC_LOCALE_ENCODING_DATA_H

#include "src/locale/locale_data.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

// Exact encoder maps and possibly non-injective decoder maps are independent.
// The immutable backing file must outlive every lookup.
class ChineseEncodingData {
public:
  enum Set : unsigned { GB2312, CNS1, CNS2, SET_COUNT };
  static ErrorOr<ChineseEncodingData> read(const LocaleDataView &file);
  ErrorOr<uint32_t> decode(Set set, uint32_t position) const;
  ErrorOr<uint32_t> encode(Set set, uint32_t scalar) const;

private:
  ChineseEncodingData() = default;
  LocaleDataView::Item decoders[SET_COUNT];
  LocaleDataView::Item encoders[SET_COUNT];
};

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_LOCALE_ENCODING_DATA_H
